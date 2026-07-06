#pragma once
#ifndef GROOVYDAISY_CAPTURE_H
#define GROOVYDAISY_CAPTURE_H

#include <stdint.h>
#include <atomic>
#include "track.h"

/**
 * GroovyDaisy retrospective capture (v2) — MIDI half.
 *
 * Rolling rings listen to everything, always; nothing is committed until
 * Capture lifts the last N bars into a brand-new track (SPEC.md capture
 * workflow). There is no record-arm anywhere in the box.
 *
 * Concurrency: the audio callback Push()es into the rings (router record
 * hook); the main loop extracts windows at capture commit. The ring is
 * append-only with an atomic head — extraction reads entries strictly
 * below the acquired head. The ring is large enough (1024 events/source)
 * that overwrite-during-extraction is not a practical concern for an
 * 8-bar window; a defensive head-delta check aborts if it ever happens.
 *
 * The audio half (guitar ring + granule pool) lands in Phase 3.
 */

namespace Capture
{

constexpr uint32_t TICKS_PER_BAR = Track::TICKS_PER_BAR; // 384
constexpr size_t   RING_EVENTS   = 1024;
constexpr uint8_t  MAX_BARS      = 8;

// Early-downbeat grace: a note played within a 32nd note BEFORE the
// window's opening bar line was meant for that downbeat — the window
// pulls it in and sits it exactly on the bar line instead of dropping
// it (nothing punishes). Always on; quantize then has nothing to move.
constexpr uint32_t EARLY_GRACE = TICKS_PER_BAR / 32; // 12 ticks

enum class Source : uint8_t
{
    Pads   = 0,
    Keys   = 1,
    Guitar = 2, // Phase 3
};

struct RingEv
{
    uint32_t tick; // absolute clock tick at record time
    uint8_t  status;
    uint8_t  d1;
    uint8_t  d2;
};

/** Append-only MIDI history ring. Writer: audio callback. Reader: main loop. */
class MidiRing
{
  public:
    void Reset() { head_.store(0, std::memory_order_relaxed); }

    /** Audio-callback context. */
    void Push(uint32_t tick, uint8_t status, uint8_t d1, uint8_t d2)
    {
        uint32_t h            = head_.load(std::memory_order_relaxed);
        buf_[h % RING_EVENTS] = {tick, status, d1, d2};
        head_.store(h + 1, std::memory_order_release);
    }

    uint32_t Head() const { return head_.load(std::memory_order_acquire); }

    const RingEv& At(uint32_t abs_index) const
    {
        return buf_[abs_index % RING_EVENTS];
    }

  private:
    RingEv                buf_[RING_EVENTS];
    std::atomic<uint32_t> head_{0};
};

/**
 * Nearest-bar rounding for the capture window end: pressing a beat late
 * still grabs the phrase you meant. If the nearest boundary is ahead of
 * `now`, the capture goes PENDING and commits when the clock crosses it.
 */
inline uint32_t WindowEndTick(uint32_t now_tick)
{
    return ((now_tick + TICKS_PER_BAR / 2) / TICKS_PER_BAR) * TICKS_PER_BAR;
}

enum class ExtractResult : uint8_t
{
    Ok = 0,
    Empty,      // no events in the window — nothing was played
    Overrun,    // ring wrapped past the window during extraction
};

/**
 * Extract [end - bars*384, end) from the ring into a track's event array.
 *
 * Event positions are rebased as `tick % length_ticks` — NOT tick-start —
 * because window boundaries are bar-aligned and playback runs at
 * global_tick % length: the modulo preserves each event's phase against
 * the global bar grid, which is what keeps every track aligned (SPEC.md
 * track model). Output is sorted by position.
 *
 * Early-downbeat grace: note-ons within EARLY_GRACE ticks before the
 * window are pulled onto the opening bar line (their note-offs shift by
 * the same delta, so durations survive — the quantize rule). CCs and
 * orphan offs get no grace.
 *
 * Dangling notes: a note-on inside the window without its note-off gets
 * one synthesized at the window's final tick; orphan note-offs (note-on
 * predates the window) are dropped.
 */
inline ExtractResult ExtractWindow(const MidiRing& ring, uint32_t end_tick,
                                   uint8_t bars, Track::MidiEv* out,
                                   uint16_t max_out, uint16_t& out_count)
{
    const uint32_t length_ticks = (uint32_t)bars * TICKS_PER_BAR;
    const uint32_t start_tick   = end_tick - length_ticks;
    const uint32_t grace_start
        = start_tick >= EARLY_GRACE ? start_tick - EARLY_GRACE : 0;

    const uint32_t head  = ring.Head();
    const uint32_t first = head > RING_EVENTS ? head - RING_EVENTS : 0;

    uint16_t n = 0;
    // Sounding-note tracking for dangling/orphan handling (128 notes,
    // synth channel only — drums are one-shots)
    uint32_t note_on_seen[4] = {0, 0, 0, 0};
    // Pull-to-bar-line shift of each note's sounding on (0 = not early)
    uint8_t early_delta[128] = {0};

    for(uint32_t i = first; i < head; i++)
    {
        const RingEv ev = ring.At(i);
        if(ev.tick < grace_start || ev.tick >= end_tick)
        {
            continue;
        }
        const bool early = ev.tick < start_tick;

        uint8_t type    = ev.status & 0xF0;
        bool    is_on   = type == 0x90 && ev.d2 > 0;
        bool    is_off  = type == 0x80 || (type == 0x90 && ev.d2 == 0);
        uint8_t channel = ev.status & 0x0F;
        bool    synth   = channel != 9; // drum channel one-shots need no offs

        // Grace zone admits notes only: a CC nudge or a stray drum off
        // just before the bar line wasn't a downbeat
        if(early && !(is_on || (synth && is_off)))
        {
            continue;
        }

        uint32_t place = ev.tick;

        if(synth && is_off)
        {
            uint32_t bit = 1u << (ev.d1 & 31);
            if(!(note_on_seen[(ev.d1 >> 5) & 3] & bit))
            {
                continue; // orphan note-off: its note-on predates the window
            }
            note_on_seen[(ev.d1 >> 5) & 3] &= ~bit;
            // Travels with its (possibly pulled-early) note-on
            place += early_delta[ev.d1];
            if(place >= end_tick)
            {
                place = end_tick - 1;
            }
            early_delta[ev.d1] = 0;
        }
        if(is_on)
        {
            if(early)
            {
                place = start_tick; // the downbeat you meant
                if(synth)
                {
                    early_delta[ev.d1] = (uint8_t)(start_tick - ev.tick);
                }
            }
            else if(synth)
            {
                early_delta[ev.d1] = 0; // retrigger: stale shift dies
            }
            if(synth)
            {
                note_on_seen[(ev.d1 >> 5) & 3] |= 1u << (ev.d1 & 31);
            }
        }

        if(n >= max_out)
        {
            break; // full: keep what fits (512 events is a LOT of jamming)
        }
        out[n++] = {place % length_ticks, ev.status, ev.d1, ev.d2};
    }

    // Ring must not have lapped the window while we read it
    if(ring.Head() - head > RING_EVENTS - 1)
    {
        out_count = 0;
        return ExtractResult::Overrun;
    }

    if(n == 0)
    {
        out_count = 0;
        return ExtractResult::Empty;
    }

    // Synthesize note-offs for notes still sounding at window end, at the
    // window's final position ((end-1) % length == position just before
    // the window-start position — the note sustains across the loop seam)
    const uint32_t off_pos = (end_tick - 1) % length_ticks;
    for(uint8_t note = 0; note < 128 && n < max_out; note++)
    {
        if(note_on_seen[note >> 5] & (1u << (note & 31)))
        {
            out[n++] = {off_pos, (uint8_t)0x80, note, 0};
        }
    }

    // Sort by loop position (stable insertion sort; n <= 512, main loop)
    for(uint16_t i = 1; i < n; i++)
    {
        Track::MidiEv key = out[i];
        int32_t       j   = i - 1;
        while(j >= 0 && out[j].tick > key.tick)
        {
            out[j + 1] = out[j];
            j--;
        }
        out[j + 1] = key;
    }

    out_count = n;
    return ExtractResult::Ok;
}

/** One in-flight capture request per source. */
struct Pending
{
    bool     armed = false;
    uint8_t  bars  = 4;
    uint32_t end_tick = 0;
};

// ---------------------------------------------------------------------------
// Audio half: the guitar's rolling buffer.
//
// The audio callback writes every input sample (s16) while the grid runs;
// at each bar boundary it records an anchor {tick, write_pos} — the exact
// sample position of that bar line. Capture windows are then cut at
// anchors, so the tick<->sample mapping is exact by construction, immune
// to any accumulated float error. Reader (CopyJob, main loop) trails the
// writer by design; the ring is sized with margin over the largest window.
// ---------------------------------------------------------------------------

struct BarAnchor
{
    uint32_t tick;       // bar-boundary tick (multiple of 384)
    uint32_t sample_pos; // absolute write position at that tick
};

constexpr size_t AUDIO_ANCHORS = 16; // > MAX_BARS of look-back

class AudioRing
{
  public:
    void Init(int16_t* mem, uint32_t capacity_samples)
    {
        mem_      = mem;
        capacity_ = capacity_samples;
        Reset();
    }

    void Reset()
    {
        write_pos_.store(0, std::memory_order_relaxed);
        anchor_count_.store(0, std::memory_order_relaxed);
        level_accum_ = 0.0f;
        level_.store(0.0f, std::memory_order_relaxed);
    }

    /** Audio-callback context: one input sample. */
    inline void Write(float in)
    {
        uint32_t p = write_pos_.load(std::memory_order_relaxed);
        float    v = in * 32767.0f;
        if(v > 32767.0f)
            v = 32767.0f;
        if(v < -32768.0f)
            v = -32768.0f;
        mem_[p % capacity_] = (int16_t)v;
        write_pos_.store(p + 1, std::memory_order_release);

        // Cheap envelope follower for the "guitarist is playing" pulse
        float a = in < 0 ? -in : in;
        level_accum_ += 0.001f * (a - level_accum_);
        if((p & 1023) == 0)
        {
            level_.store(level_accum_, std::memory_order_relaxed);
        }
    }

    /** Audio-callback context: called on each bar-boundary tick. */
    void AnchorBar(uint32_t tick)
    {
        uint32_t n = anchor_count_.load(std::memory_order_relaxed);
        anchors_[n % AUDIO_ANCHORS]
            = {tick, write_pos_.load(std::memory_order_relaxed)};
        anchor_count_.store(n + 1, std::memory_order_release);
    }

    /** Main loop: sample position of the bar line at `tick`, or false. */
    bool FindAnchor(uint32_t tick, uint32_t& out_pos) const
    {
        uint32_t n     = anchor_count_.load(std::memory_order_acquire);
        uint32_t first = n > AUDIO_ANCHORS ? n - AUDIO_ANCHORS : 0;
        for(uint32_t i = first; i < n; i++)
        {
            const BarAnchor& a = anchors_[i % AUDIO_ANCHORS];
            if(a.tick == tick)
            {
                out_pos = a.sample_pos;
                return true;
            }
        }
        return false;
    }

    uint32_t WritePos() const
    {
        return write_pos_.load(std::memory_order_acquire);
    }

    int16_t At(uint32_t abs_pos) const { return mem_[abs_pos % capacity_]; }

    uint32_t Capacity() const { return capacity_; }

    /** Envelope level 0..1-ish for activity display (main loop). */
    float Level() const { return level_.load(std::memory_order_relaxed); }

  private:
    int16_t*              mem_      = nullptr;
    uint32_t              capacity_ = 0;
    BarAnchor             anchors_[AUDIO_ANCHORS];
    std::atomic<uint32_t> write_pos_{0};
    std::atomic<uint32_t> anchor_count_{0};
    float                 level_accum_ = 0.0f;
    std::atomic<float>    level_{0.0f};
};

} // namespace Capture

#endif // GROOVYDAISY_CAPTURE_H
