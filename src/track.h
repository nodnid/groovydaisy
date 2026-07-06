#pragma once
#ifndef GROOVYDAISY_TRACK_H
#define GROOVYDAISY_TRACK_H

#include <stdint.h>
#include <atomic>

/**
 * GroovyDaisy track registry (v2).
 *
 * Every capture creates a track; a track's kind is the source it was
 * captured from (SPEC.md track model). Storage is static — Track slots
 * are reused, and each reuse bumps `gen` so the protocol address
 * (slot, gen) can never confuse a new track with a dead one.
 *
 * Concurrency contract (why this needs no locks):
 * - Registry mutation (Create/Activate/Destroy) happens in the MAIN LOOP
 *   only. The audio callback only reads.
 * - A slot's payload (events) is filled BEFORE Activate() publishes it
 *   with a release store; the callback acquires `active` before touching
 *   payload. Content of a live track is never mutated in place — that's
 *   the payoff of capture-creates-a-new-track.
 * - Callback-owned playback state (playback_idx, notes_on, prev_mute)
 *   is never touched by the main loop while active.
 */

namespace Track
{

constexpr int      MAX_TRACKS    = 32;
constexpr int      MAX_PER_KIND  = 16;
constexpr int      MAX_EVENTS    = 512;
constexpr uint32_t TICKS_PER_BAR = 384; // mirrors clock.h
constexpr int      MAX_AUTO_CC   = 8;   // automatable CCs (groove.h table)

enum class Kind : uint8_t
{
    MidiDrum  = 0,
    MidiSynth = 1,
    Audio     = 2,
};

struct MidiEv
{
    uint32_t tick; // position within the loop, 0..LengthTicks()-1
    uint8_t  status;
    uint8_t  d1;
    uint8_t  d2;
};

struct Slot
{
    std::atomic<bool> active{false};
    uint8_t           gen         = 0;
    Kind              kind        = Kind::MidiDrum;
    uint8_t           length_bars = 4;
    uint16_t          created_seq = 0;

    // MIDI payload (filled before Activate; immutable while active)
    MidiEv   events[MAX_EVENTS];
    uint16_t event_count = 0;

    // CC-blend base: live knob positions at capture commit (groove.h
    // AUTO_CCS order). Playback applies recorded + (live - base).
    uint8_t cc_base[MAX_AUTO_CC] = {64, 64, 64, 64, 64, 64, 64, 64};

    // Audio payload (Kind::Audio): granule chain in LOOP-POSITION order —
    // chain[b] holds the audio for loop bar b. Filled before Activate.
    uint16_t chain[8]       = {0};
    uint32_t samples_per_bar = 0;
    // Waveform peaks for the UI (24/bar, u8), kept for snapshot resends
    uint8_t  peaks[192];
    uint16_t peak_count = 0;

    // ---- audio-callback-owned audio playback state ----
    // Resynced to the grid at every bar tick (sub-sample corrections);
    // free-runs between resyncs.
    uint32_t play_gran_off = 0; // offset within the current bar's granule
    uint8_t  play_bar      = 0; // current loop bar

    // ---- audio-callback-owned playback state ----
    uint32_t last_tick_mod = 0xFFFFFFFF; // detects jumps/wraps
    uint16_t playback_idx  = 0;
    uint32_t notes_on[4]   = {0, 0, 0, 0}; // sounding notes (synth kind)
    bool     prev_mute     = false;

    // main-loop bookkeeping: slot handed out by Create() but not yet
    // Activate()d (capture commit in progress)
    bool reserved_ = false;

    uint32_t LengthTicks() const { return (uint32_t)length_bars * TICKS_PER_BAR; }
};

class Registry
{
  public:
    void Init()
    {
        for(int i = 0; i < MAX_TRACKS; i++)
        {
            slots_[i].active.store(false, std::memory_order_relaxed);
            slots_[i].gen = 0;
        }
        next_seq_ = 1;
    }

    /**
     * Reserve a free slot for a new track (main loop). Payload is filled
     * by the caller, then Activate() publishes it. Returns -1 if the
     * per-kind cap or the slot pool is exhausted.
     */
    int Create(Kind kind, uint8_t length_bars)
    {
        if(CountKind(kind) >= MAX_PER_KIND)
        {
            return -1;
        }
        for(int i = 0; i < MAX_TRACKS; i++)
        {
            Slot& s = slots_[i];
            if(!s.active.load(std::memory_order_relaxed) && !s.reserved_)
            {
                s.reserved_    = true;
                s.kind         = kind;
                s.length_bars  = length_bars;
                s.created_seq  = next_seq_++;
                s.event_count  = 0;
                s.last_tick_mod = 0xFFFFFFFF;
                s.playback_idx = 0;
                for(int b = 0; b < 4; b++)
                    s.notes_on[b] = 0;
                s.prev_mute = false;
                return i;
            }
        }
        return -1;
    }

    /** Publish a filled slot to the audio callback. */
    void Activate(int slot)
    {
        slots_[slot].reserved_ = false;
        slots_[slot].active.store(true, std::memory_order_release);
    }

    /** Abandon a reserved-but-never-activated slot. */
    void Abort(int slot) { slots_[slot].reserved_ = false; }

    /**
     * Remove a track (delete or undo). gen bump makes stale (slot, gen)
     * references detectable. The callback observes active==false and
     * releases its notes (see seq_track.h).
     */
    void Destroy(int slot)
    {
        slots_[slot].active.store(false, std::memory_order_release);
        slots_[slot].gen++;
    }

    /** Newest active track by creation order (undo target), or -1. */
    int NewestActive() const
    {
        int      best     = -1;
        uint16_t best_seq = 0;
        for(int i = 0; i < MAX_TRACKS; i++)
        {
            const Slot& s = slots_[i];
            if(s.active.load(std::memory_order_relaxed)
               && s.created_seq >= best_seq)
            {
                best_seq = s.created_seq;
                best     = i;
            }
        }
        return best;
    }

    int CountKind(Kind kind) const
    {
        int n = 0;
        for(int i = 0; i < MAX_TRACKS; i++)
        {
            if(slots_[i].active.load(std::memory_order_relaxed)
               && slots_[i].kind == kind)
            {
                n++;
            }
        }
        return n;
    }

    int CountActive() const
    {
        int n = 0;
        for(int i = 0; i < MAX_TRACKS; i++)
        {
            if(slots_[i].active.load(std::memory_order_relaxed))
                n++;
        }
        return n;
    }

    Slot&       Get(int slot) { return slots_[slot]; }
    const Slot& Get(int slot) const { return slots_[slot]; }

  private:
    Slot     slots_[MAX_TRACKS];
    uint16_t next_seq_ = 1;
};

} // namespace Track

#endif // GROOVYDAISY_TRACK_H
