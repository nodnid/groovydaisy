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

    const uint32_t head  = ring.Head();
    const uint32_t first = head > RING_EVENTS ? head - RING_EVENTS : 0;

    uint16_t n = 0;
    // Sounding-note tracking for dangling/orphan handling (128 notes,
    // synth channel only — drums are one-shots)
    uint32_t note_on_seen[4] = {0, 0, 0, 0};

    for(uint32_t i = first; i < head; i++)
    {
        const RingEv ev = ring.At(i);
        if(ev.tick < start_tick || ev.tick >= end_tick)
        {
            continue;
        }

        uint8_t type    = ev.status & 0xF0;
        bool    is_on   = type == 0x90 && ev.d2 > 0;
        bool    is_off  = type == 0x80 || (type == 0x90 && ev.d2 == 0);
        uint8_t channel = ev.status & 0x0F;
        bool    synth   = channel != 9; // drum channel one-shots need no offs

        if(synth && is_off)
        {
            uint32_t bit = 1u << (ev.d1 & 31);
            if(!(note_on_seen[(ev.d1 >> 5) & 3] & bit))
            {
                continue; // orphan note-off: its note-on predates the window
            }
            note_on_seen[(ev.d1 >> 5) & 3] &= ~bit;
        }
        if(synth && is_on)
        {
            note_on_seen[(ev.d1 >> 5) & 3] |= 1u << (ev.d1 & 31);
        }

        if(n >= max_out)
        {
            break; // full: keep what fits (512 events is a LOT of jamming)
        }
        out[n++] = {ev.tick % length_ticks, ev.status, ev.d1, ev.d2};
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

} // namespace Capture

#endif // GROOVYDAISY_CAPTURE_H
