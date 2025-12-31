#pragma once
#ifndef GROOVYDAISY_SEQUENCER_H
#define GROOVYDAISY_SEQUENCER_H

#include <stdint.h>

/**
 * GroovyDaisy MIDI Recording Sequencer
 *
 * Records and plays back MIDI events with tick-accurate timing.
 * Supports 8 drum tracks + 4 synth tracks, overdub and replace modes.
 * Uses callback for playback to route through unified MIDI router.
 */

namespace Sequencer
{

// Constants
constexpr uint16_t MAX_EVENTS_PER_TRACK = 512;
constexpr uint8_t  NUM_DRUM_TRACKS      = 8;
constexpr uint8_t  NUM_SYNTH_TRACKS     = 4;
constexpr uint8_t  NUM_TOTAL_TRACKS     = NUM_DRUM_TRACKS + NUM_SYNTH_TRACKS;

// MIDI channel constants
constexpr uint8_t DRUM_CHANNEL  = 9;   // Channel 10 (0-indexed)
constexpr uint8_t SYNTH_CHANNEL = 0;   // Channel 1 (0-indexed)

// Drum pad note range
constexpr uint8_t FIRST_PAD_NOTE = 36;  // KeyLab pads: notes 36-43
constexpr uint8_t LAST_PAD_NOTE  = 43;

// Callback type for playback events (goes to MIDI router)
typedef void (*PlaybackCallback)(uint8_t status, uint8_t data1, uint8_t data2);

/**
 * Single MIDI event with timestamp
 */
struct MidiEvent
{
    uint32_t tick;    // Position in pattern (0 to pattern_ticks-1)
    uint8_t  status;  // 0x90=NoteOn, 0x80=NoteOff, 0xB0=CC
    uint8_t  data1;   // Note number or CC number
    uint8_t  data2;   // Velocity or CC value
};

/**
 * Single track containing recorded events
 */
struct Track
{
    MidiEvent events[MAX_EVENTS_PER_TRACK];
    uint16_t  event_count;
    uint16_t  playback_index;  // For efficient playback scanning

    void Clear()
    {
        event_count    = 0;
        playback_index = 0;
    }

    void ResetPlayback() { playback_index = 0; }
};

/**
 * Main sequencer engine
 */
class Engine
{
  public:
    /**
     * Initialize the sequencer
     * @param pattern_length Pattern length in ticks (e.g., 1536 for 4 bars at 96 PPQN)
     */
    void Init(uint32_t pattern_length)
    {
        pattern_length_      = pattern_length;
        last_tick_           = 0;
        overdub_mode_        = true;   // Default to overdub
        first_note_in_pass_  = false;
        playback_cb_         = nullptr;

        // Clear all tracks (drums + synth)
        for(uint8_t i = 0; i < NUM_TOTAL_TRACKS; i++)
        {
            tracks_[i].Clear();
        }
    }

    /**
     * Set callback for playback events
     * This should route to the MIDI router for unified handling
     */
    void SetPlaybackCallback(PlaybackCallback cb) { playback_cb_ = cb; }

    /**
     * Record a MIDI event at the current tick position
     * Events are inserted in sorted order by tick
     *
     * Supports:
     * - Drum notes (channel 10, notes 36-43) -> drum tracks 0-7
     * - Synth notes (channel 1, any note) -> synth tracks (hashed by note)
     */
    void RecordEvent(uint32_t tick, uint8_t status, uint8_t data1, uint8_t data2)
    {
        uint8_t channel = status & 0x0F;
        uint8_t type = status & 0xF0;
        Track* track = nullptr;

        // Route to appropriate track
        if(channel == DRUM_CHANNEL)
        {
            // Drum event - must be in pad range
            if(data1 < FIRST_PAD_NOTE || data1 > LAST_PAD_NOTE)
                return;

            uint8_t track_idx = data1 - FIRST_PAD_NOTE;
            track = &tracks_[track_idx];
        }
        else if(channel == SYNTH_CHANNEL && (type == 0x90 || type == 0x80))
        {
            // Synth note event - hash note to track (simple distribution)
            uint8_t track_idx = NUM_DRUM_TRACKS + (data1 % NUM_SYNTH_TRACKS);
            track = &tracks_[track_idx];
        }
        else
        {
            // Unknown event type - ignore
            return;
        }

        // Replace mode: clear track on first note of this recording pass
        if(!overdub_mode_ && first_note_in_pass_ && type == 0x90 && data2 > 0)
        {
            track->Clear();
            first_note_in_pass_ = false;
        }

        // Check capacity
        if(track->event_count >= MAX_EVENTS_PER_TRACK)
            return;

        // Find insertion point (maintain sorted order by tick)
        uint16_t insert_pos = track->event_count;
        for(uint16_t i = 0; i < track->event_count; i++)
        {
            if(track->events[i].tick > tick)
            {
                insert_pos = i;
                break;
            }
        }

        // Shift events to make room
        for(uint16_t i = track->event_count; i > insert_pos; i--)
        {
            track->events[i] = track->events[i - 1];
        }

        // Insert new event
        track->events[insert_pos].tick   = tick;
        track->events[insert_pos].status = status;
        track->events[insert_pos].data1  = data1;
        track->events[insert_pos].data2  = data2;
        track->event_count++;
    }

    /**
     * Process playback for the current tick
     * Call this once per tick when transport is playing or recording
     * Uses callback instead of direct sampler call for unified routing
     */
    void Process(uint32_t current_tick)
    {
        // Detect pattern loop (tick wrapped around)
        if(current_tick < last_tick_)
        {
            ResetPlayback();
        }
        last_tick_ = current_tick;

        // No callback = no playback
        if(playback_cb_ == nullptr)
            return;

        // Scan ALL tracks for events at current tick
        for(uint8_t t = 0; t < NUM_TOTAL_TRACKS; t++)
        {
            Track& track = tracks_[t];

            // Scan forward through events
            while(track.playback_index < track.event_count)
            {
                MidiEvent& ev = track.events[track.playback_index];

                // Future event - stop scanning this track
                if(ev.tick > current_tick)
                    break;

                // Event at current tick - route through callback
                if(ev.tick == current_tick)
                {
                    // For drums: only trigger NoteOn with velocity > 0
                    // For synth: trigger NoteOn, NoteOff, etc.
                    uint8_t channel = ev.status & 0x0F;
                    uint8_t type = ev.status & 0xF0;

                    if(channel == DRUM_CHANNEL)
                    {
                        // Drums only need NoteOn
                        if(type == 0x90 && ev.data2 > 0)
                        {
                            playback_cb_(ev.status, ev.data1, ev.data2);
                        }
                    }
                    else if(channel == SYNTH_CHANNEL)
                    {
                        // Synth needs NoteOn and NoteOff
                        if(type == 0x90 || type == 0x80)
                        {
                            playback_cb_(ev.status, ev.data1, ev.data2);
                        }
                    }
                }

                track.playback_index++;
            }
        }
    }

    /**
     * Clear all tracks
     */
    void Clear()
    {
        for(uint8_t i = 0; i < NUM_TOTAL_TRACKS; i++)
        {
            tracks_[i].Clear();
        }
    }

    /**
     * Clear a specific track
     */
    void ClearTrack(uint8_t track)
    {
        if(track < NUM_TOTAL_TRACKS)
        {
            tracks_[track].Clear();
        }
    }

    /**
     * Reset playback indices for all tracks
     * Call this when transport stops or resets
     */
    void ResetPlayback()
    {
        for(uint8_t i = 0; i < NUM_TOTAL_TRACKS; i++)
        {
            tracks_[i].ResetPlayback();
        }
        last_tick_ = 0;
    }

    /**
     * Set recording mode
     * @param overdub true for overdub (layer), false for replace (clear on first note)
     */
    void SetOverdubMode(bool overdub) { overdub_mode_ = overdub; }

    /**
     * Get current recording mode
     */
    bool IsOverdubMode() const { return overdub_mode_; }

    /**
     * Called when entering record mode
     * Resets first_note flag for replace mode
     */
    void StartRecordPass() { first_note_in_pass_ = true; }

    /**
     * Get total event count across all tracks
     */
    uint16_t GetEventCount() const
    {
        uint16_t total = 0;
        for(uint8_t i = 0; i < NUM_TOTAL_TRACKS; i++)
        {
            total += tracks_[i].event_count;
        }
        return total;
    }

    /**
     * Get event count for drum tracks only
     */
    uint16_t GetDrumEventCount() const
    {
        uint16_t total = 0;
        for(uint8_t i = 0; i < NUM_DRUM_TRACKS; i++)
        {
            total += tracks_[i].event_count;
        }
        return total;
    }

    /**
     * Get event count for synth tracks only
     */
    uint16_t GetSynthEventCount() const
    {
        uint16_t total = 0;
        for(uint8_t i = NUM_DRUM_TRACKS; i < NUM_TOTAL_TRACKS; i++)
        {
            total += tracks_[i].event_count;
        }
        return total;
    }

    /**
     * Get event count for a specific track
     */
    uint16_t GetTrackEventCount(uint8_t track) const
    {
        if(track < NUM_TOTAL_TRACKS)
        {
            return tracks_[track].event_count;
        }
        return 0;
    }

  private:
    Track    tracks_[NUM_TOTAL_TRACKS];
    uint32_t pattern_length_;
    uint32_t last_tick_;
    bool     overdub_mode_;
    bool     first_note_in_pass_;
    PlaybackCallback playback_cb_;
};

} // namespace Sequencer

#endif // GROOVYDAISY_SEQUENCER_H
