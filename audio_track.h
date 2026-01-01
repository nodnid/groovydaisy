#pragma once
#ifndef GROOVYDAISY_AUDIO_TRACK_H
#define GROOVYDAISY_AUDIO_TRACK_H

#include <stdint.h>
#include <stddef.h>

/**
 * GroovyDaisy Audio Track System
 *
 * Manages frozen (bounced) audio tracks. When a synth MIDI track is "frozen",
 * its output is rendered to an audio buffer during one pattern loop, then
 * played back as audio instead of running the synth engine.
 *
 * This trades memory for CPU - frozen tracks use ~3-12 MB each (depending on
 * tempo/bars) but free up the synth for other sounds.
 *
 * Memory budget (64 MB SDRAM):
 * - Drum samples: ~10 MB
 * - 3 frozen tracks @ 32 sec max each: ~36 MB
 * - Remaining for future features: ~18 MB
 */

namespace AudioTrack
{

// Track status states
enum class Status : uint8_t
{
    MIDI      = 0,  // Track is live, using synth engine
    PENDING   = 1,  // Waiting for pattern loop to start recording
    RENDERING = 2,  // Currently bouncing to audio buffer
    AUDIO     = 3,  // Frozen, playing from audio buffer
};

// Maximum audio buffer size per track
// 32 seconds @ 48kHz stereo = 1,536,000 samples per channel
constexpr size_t MAX_TRACK_SAMPLES = 48000 * 32;

// Number of frozen track slots (limited by SDRAM)
constexpr uint8_t NUM_FROZEN_SLOTS = 3;

// "Not frozen" marker
constexpr uint8_t NO_SLOT = 0xFF;

/**
 * A frozen audio track buffer
 *
 * Buffers are allocated statically in SDRAM, but length varies by pattern.
 * Playhead advances during playback and wraps at length.
 */
struct FrozenSlot
{
    float*  buffer_L;       // Pointer to left channel buffer in SDRAM
    float*  buffer_R;       // Pointer to right channel buffer in SDRAM
    size_t  length;         // Actual samples used (varies by tempo/bars)
    size_t  playhead;       // Current playback position
    uint8_t source_track;   // Which MIDI track (8-11) this came from
    bool    in_use;         // Whether this slot is occupied

    /**
     * Initialize a frozen slot with buffer pointers
     */
    void Init(float* buf_l, float* buf_r)
    {
        buffer_L     = buf_l;
        buffer_R     = buf_r;
        length       = 0;
        playhead     = 0;
        source_track = 0;
        in_use       = false;
    }

    /**
     * Clear the slot, making it available
     */
    void Clear()
    {
        length       = 0;
        playhead     = 0;
        source_track = 0;
        in_use       = false;
    }

    /**
     * Reset playhead to beginning
     */
    void ResetPlayhead()
    {
        playhead = 0;
    }

    /**
     * Read a stereo sample at current playhead and advance
     * Returns (0, 0) if past end or not in use
     */
    void ReadAndAdvance(float& out_l, float& out_r)
    {
        if(!in_use || playhead >= length)
        {
            out_l = 0.0f;
            out_r = 0.0f;
            return;
        }

        out_l = buffer_L[playhead];
        out_r = buffer_R[playhead];
        playhead++;

        // Wrap at pattern end
        if(playhead >= length)
        {
            playhead = 0;
        }
    }

    /**
     * Write a stereo sample at current render position and advance
     * Used during the RENDERING phase
     */
    void WriteAndAdvance(float in_l, float in_r)
    {
        if(playhead < MAX_TRACK_SAMPLES)
        {
            buffer_L[playhead] = in_l;
            buffer_R[playhead] = in_r;
            playhead++;
        }
    }

    /**
     * Finalize rendering - set actual length and reset playhead
     */
    void FinalizeRender()
    {
        length   = playhead;
        playhead = 0;
    }
};

/**
 * Track state for a synth MIDI track
 */
struct TrackState
{
    Status  status;         // Current status
    uint8_t frozen_slot;    // Which frozen slot (0-2), or NO_SLOT if MIDI

    void Init()
    {
        status      = Status::MIDI;
        frozen_slot = NO_SLOT;
    }
};

/**
 * Audio track manager
 *
 * Manages 4 synth tracks and 3 frozen audio slots.
 * Synth tracks are indices 8-11 (after the 8 drum tracks).
 */
class Manager
{
  public:
    static constexpr uint8_t NUM_SYNTH_TRACKS = 4;

    /**
     * Initialize the manager with SDRAM buffer pointers
     */
    void Init(float* buf_l[NUM_FROZEN_SLOTS], float* buf_r[NUM_FROZEN_SLOTS])
    {
        for(uint8_t i = 0; i < NUM_FROZEN_SLOTS; i++)
        {
            slots_[i].Init(buf_l[i], buf_r[i]);
        }

        for(uint8_t i = 0; i < NUM_SYNTH_TRACKS; i++)
        {
            tracks_[i].Init();
        }

        render_target_ = NO_SLOT;
        pending_slot_  = NO_SLOT;
    }

    /**
     * Get track state for a synth track (0-3)
     */
    const TrackState& GetTrackState(uint8_t synth_track) const
    {
        return tracks_[synth_track < NUM_SYNTH_TRACKS ? synth_track : 0];
    }

    /**
     * Check if a synth track is frozen (playing audio)
     */
    bool IsTrackFrozen(uint8_t synth_track) const
    {
        if(synth_track >= NUM_SYNTH_TRACKS)
            return false;
        return tracks_[synth_track].status == Status::AUDIO;
    }

    /**
     * Check if a synth track is currently rendering
     */
    bool IsTrackRendering(uint8_t synth_track) const
    {
        if(synth_track >= NUM_SYNTH_TRACKS)
            return false;
        return tracks_[synth_track].status == Status::RENDERING;
    }

    /**
     * Check if any track is frozen (for tempo lock)
     */
    bool HasFrozenTracks() const
    {
        for(uint8_t i = 0; i < NUM_SYNTH_TRACKS; i++)
        {
            if(tracks_[i].status == Status::AUDIO)
                return true;
        }
        return false;
    }

    /**
     * Start freezing a track - sets to PENDING state
     * Actual recording begins when BeginRecording() is called (on pattern loop)
     * Returns false if no slots available or track already frozen
     */
    bool StartFreeze(uint8_t synth_track)
    {
        if(synth_track >= NUM_SYNTH_TRACKS)
            return false;

        TrackState& track = tracks_[synth_track];

        // Already frozen, pending, or rendering
        if(track.status != Status::MIDI)
            return false;

        // Find an available slot
        for(uint8_t i = 0; i < NUM_FROZEN_SLOTS; i++)
        {
            if(!slots_[i].in_use)
            {
                // Allocate slot
                slots_[i].in_use       = true;
                slots_[i].source_track = synth_track;
                slots_[i].playhead     = 0;
                slots_[i].length       = 0;

                // Update track state - set to PENDING, wait for pattern loop
                track.status      = Status::PENDING;
                track.frozen_slot = i;
                pending_slot_     = i;  // Track pending slot for BeginRecording

                return true;
            }
        }

        return false;  // No slots available
    }

    /**
     * Begin recording - called when pattern loops while in PENDING state
     * Transitions from PENDING to RENDERING
     */
    bool BeginRecording()
    {
        if(pending_slot_ == NO_SLOT)
            return false;

        // Find the track with this pending slot
        for(uint8_t i = 0; i < NUM_SYNTH_TRACKS; i++)
        {
            if(tracks_[i].frozen_slot == pending_slot_
               && tracks_[i].status == Status::PENDING)
            {
                tracks_[i].status = Status::RENDERING;
                slots_[pending_slot_].playhead = 0;  // Reset playhead for recording
                render_target_ = pending_slot_;
                pending_slot_ = NO_SLOT;
                return true;
            }
        }

        return false;
    }

    /**
     * Check if a freeze is pending (waiting for pattern loop to start)
     */
    bool HasPendingFreeze() const
    {
        return pending_slot_ != NO_SLOT;
    }

    /**
     * Finalize a freeze operation after pattern loop completes
     * Call this when the pattern loops and rendering is done
     */
    void FinalizeFreeze()
    {
        if(render_target_ == NO_SLOT)
            return;

        FrozenSlot& slot = slots_[render_target_];
        slot.FinalizeRender();

        // Update track status to AUDIO
        for(uint8_t i = 0; i < NUM_SYNTH_TRACKS; i++)
        {
            if(tracks_[i].frozen_slot == render_target_
               && tracks_[i].status == Status::RENDERING)
            {
                tracks_[i].status = Status::AUDIO;
            }
        }

        render_target_ = NO_SLOT;
    }

    /**
     * Unfreeze a track, returning it to MIDI mode
     */
    bool Unfreeze(uint8_t synth_track)
    {
        if(synth_track >= NUM_SYNTH_TRACKS)
            return false;

        TrackState& track = tracks_[synth_track];

        if(track.status != Status::AUDIO)
            return false;

        // Free the slot
        if(track.frozen_slot < NUM_FROZEN_SLOTS)
        {
            slots_[track.frozen_slot].Clear();
        }

        track.status      = Status::MIDI;
        track.frozen_slot = NO_SLOT;

        return true;
    }

    /**
     * Write audio to the rendering track (call from audio callback)
     */
    void WriteRenderSample(float l, float r)
    {
        if(render_target_ < NUM_FROZEN_SLOTS)
        {
            slots_[render_target_].WriteAndAdvance(l, r);
        }
    }

    /**
     * Read audio from a frozen track (call from audio callback)
     */
    void ReadFrozenSample(uint8_t synth_track, float& out_l, float& out_r)
    {
        out_l = 0.0f;
        out_r = 0.0f;

        if(synth_track >= NUM_SYNTH_TRACKS)
            return;

        const TrackState& track = tracks_[synth_track];
        if(track.status != Status::AUDIO || track.frozen_slot >= NUM_FROZEN_SLOTS)
            return;

        slots_[track.frozen_slot].ReadAndAdvance(out_l, out_r);
    }

    /**
     * Reset all playheads (call on transport stop/reset)
     */
    void ResetPlayheads()
    {
        for(uint8_t i = 0; i < NUM_FROZEN_SLOTS; i++)
        {
            slots_[i].ResetPlayhead();
        }
    }

    /**
     * Get currently rendering slot (for status display)
     */
    uint8_t GetRenderTarget() const { return render_target_; }

    /**
     * Get memory used by frozen tracks in bytes
     */
    size_t GetUsedMemory() const
    {
        size_t used = 0;
        for(uint8_t i = 0; i < NUM_FROZEN_SLOTS; i++)
        {
            if(slots_[i].in_use)
            {
                // Stereo float samples
                used += slots_[i].length * sizeof(float) * 2;
            }
        }
        return used;
    }

    /**
     * Get number of available frozen slots
     */
    uint8_t GetAvailableSlots() const
    {
        uint8_t count = 0;
        for(uint8_t i = 0; i < NUM_FROZEN_SLOTS; i++)
        {
            if(!slots_[i].in_use)
                count++;
        }
        return count;
    }

  private:
    FrozenSlot slots_[NUM_FROZEN_SLOTS];
    TrackState tracks_[NUM_SYNTH_TRACKS];
    uint8_t    render_target_;  // Currently rendering slot, or NO_SLOT
    uint8_t    pending_slot_;   // Slot waiting for pattern loop to start, or NO_SLOT
};

}  // namespace AudioTrack

#endif  // GROOVYDAISY_AUDIO_TRACK_H
