#pragma once
#ifndef GROOVYDAISY_GROOVE_H
#define GROOVYDAISY_GROOVE_H

#include <stdint.h>
#include <string.h>
#include <cmath>
#include "track.h"

/**
 * GroovyDaisy groove intelligence (Phase 4).
 *
 * Four small machines that make MIDI capture musical (ROADMAP.md Phase 4):
 *
 * - Quantize at capture: off / light (50% toward nearest 16th) / hard.
 *   Applied destructively at capture commit (SPEC.md quantize) — the ring
 *   keeps the raw performance until then, so "non-destructive" already
 *   exists in the form of not pressing Capture. Note-offs travel with
 *   their note-ons so durations survive.
 *
 * - Swing: a playback-time tick warp, never stored — live-tweakable and
 *   non-destructive. Each 8th note (48 ticks) is warped piecewise-linear:
 *   the first 16th stretches, the second compresses, bar lines are fixed
 *   points. 50% = straight, 75% = maximum shuffle (offbeat 16th delayed
 *   half a 16th).
 *
 * - Velocity compression: optional power-curve lift for drum captures.
 *   Campfire pad technique is wild; taming is kind.
 *
 * - CC automation plumbing: the canonical automatable-CC table (indices
 *   shared by ring events, track base values and live knob tracking), a
 *   record thinner (min-interval + min-delta, from v1), and the blend
 *   math `effective = recorded + (live − base_at_commit)` (SPEC.md CC
 *   automation; base at commit fixes v1's stale-base bug).
 *
 * Host-compilable: pure math over track.h types, no engine includes.
 */

namespace Groove
{

constexpr uint32_t GRID = Track::TICKS_PER_BAR / 16; // 16th note = 24 ticks
constexpr uint32_t CELL = 2 * GRID;                  // 8th note  = 48 ticks

// --------------------------------------------------------------------------
// Quantize
// --------------------------------------------------------------------------

enum class Quant : uint8_t
{
    Off   = 0,
    Light = 1, // 50% toward the nearest 16th
    Hard  = 2, // snap to the nearest 16th
};

/** Signed tick shift that quantizes `tick` per `mode`. */
inline int32_t QuantDelta(uint32_t tick, Quant mode)
{
    if(mode == Quant::Off)
    {
        return 0;
    }
    uint32_t grid = ((tick + GRID / 2) / GRID) * GRID;
    int32_t  d    = (int32_t)grid - (int32_t)tick;
    return mode == Quant::Light ? d / 2 : d;
}

inline uint32_t WrapTick(int32_t tick, uint32_t len)
{
    // |delta| <= GRID/2 << len, so one correction each way suffices
    if(tick < 0)
        tick += (int32_t)len;
    if(tick >= (int32_t)len)
        tick -= (int32_t)len;
    return (uint32_t)tick;
}

/**
 * Quantize a committed capture in place. Note-ons move to (or toward) the
 * 16th grid; each note-off travels by ITS note-on's delta so the duration
 * is preserved. Drum-channel hits (one-shots, no offs) just move. CC
 * events (0xB0) are never touched. Events are re-sorted afterward.
 *
 * Pairing is wrap-aware: a note that sustains across the loop seam has
 * its off EARLIER in the (loop-position-sorted) array than its on; such
 * orphan offs pair with the note's last unmatched on.
 */
inline void QuantizeEvents(Track::MidiEv* ev, uint16_t count,
                           uint32_t len_ticks, Quant mode)
{
    if(mode == Quant::Off || count == 0)
    {
        return;
    }

    int8_t  note_delta[128];  // pending on's delta, per note
    int16_t orphan_off[128];  // off seen before its wrapped on
    uint32_t pending[4] = {0, 0, 0, 0};
    for(int i = 0; i < 128; i++)
        orphan_off[i] = -1;
    memset(note_delta, 0, sizeof(note_delta));

    for(uint16_t i = 0; i < count; i++)
    {
        uint8_t type    = ev[i].status & 0xF0;
        uint8_t channel = ev[i].status & 0x0F;
        bool    is_on   = type == 0x90 && ev[i].d2 > 0;
        bool    is_off  = type == 0x80 || (type == 0x90 && ev[i].d2 == 0);
        uint8_t note    = ev[i].d1;

        if(channel == 9) // drums: one-shots, every hit moves independently
        {
            if(is_on)
            {
                ev[i].tick = WrapTick(
                    (int32_t)ev[i].tick + QuantDelta(ev[i].tick, mode),
                    len_ticks);
            }
            continue;
        }

        if(is_on)
        {
            int32_t d = QuantDelta(ev[i].tick, mode);
            ev[i].tick = WrapTick((int32_t)ev[i].tick + d, len_ticks);
            note_delta[note] = (int8_t)d;
            pending[note >> 5] |= 1u << (note & 31);
        }
        else if(is_off)
        {
            if(pending[note >> 5] & (1u << (note & 31)))
            {
                ev[i].tick = WrapTick(
                    (int32_t)ev[i].tick + note_delta[note], len_ticks);
                pending[note >> 5] &= ~(1u << (note & 31));
            }
            else
            {
                orphan_off[note] = (int16_t)i; // wrapped pair: on comes later
            }
        }
        // CC / anything else: untouched
    }

    // Orphan offs travel with the note's still-unmatched (wrapped) on
    for(int note = 0; note < 128; note++)
    {
        if(orphan_off[note] >= 0 && (pending[note >> 5] & (1u << (note & 31))))
        {
            Track::MidiEv& o = ev[orphan_off[note]];
            o.tick = WrapTick((int32_t)o.tick + note_delta[note], len_ticks);
        }
    }

    // Re-sort by loop position (stable insertion, count <= 512, main loop)
    for(uint16_t i = 1; i < count; i++)
    {
        Track::MidiEv key = ev[i];
        int32_t       j   = i - 1;
        while(j >= 0 && ev[j].tick > key.tick)
        {
            ev[j + 1] = ev[j];
            j--;
        }
        ev[j + 1] = key;
    }
}

// --------------------------------------------------------------------------
// Swing (playback-time tick warp — non-destructive, live-tweakable)
// --------------------------------------------------------------------------

constexpr uint8_t MAX_SWING_TICKS = (uint8_t)(CELL / 4); // 12 = 75%

/** UI percent (50..75) -> warp ticks (0..12). */
inline uint8_t SwingPctToTicks(uint8_t pct)
{
    if(pct < 50)
        pct = 50;
    if(pct > 75)
        pct = 75;
    return (uint8_t)(((uint32_t)(pct - 50) * CELL + 50) / 100);
}

/** Warp ticks -> UI percent (for state echo). */
inline uint8_t SwingTicksToPct(uint8_t s)
{
    return (uint8_t)(50 + ((uint32_t)s * 100 + CELL / 2) / CELL);
}

/**
 * Map a straight event tick to its swung dispatch tick. Monotonic
 * (non-decreasing) in `tick`, so sorted event arrays stay dispatch-
 * ordered; multiples of CELL are fixed points, so bar lines and loop
 * lengths never move. Warped ticks never leave their 8th-note cell.
 */
inline uint32_t SwingWarp(uint32_t tick, uint8_t s)
{
    if(s == 0)
    {
        return tick;
    }
    uint32_t cell = tick / CELL * CELL;
    uint32_t p    = tick - cell;
    uint32_t w;
    if(p < GRID)
    {
        w = p * (GRID + s) / GRID; // first 16th stretches
    }
    else
    {
        w = GRID + s + (p - GRID) * (GRID - s) / GRID; // second compresses
    }
    return cell + w;
}

// --------------------------------------------------------------------------
// Velocity compression (drum captures)
// --------------------------------------------------------------------------

/** Power-curve lift: 127 stays 127, soft hits come up (x^0.6). */
inline uint8_t CompressVelocity(uint8_t vel)
{
    if(vel == 0)
    {
        return 0;
    }
    float v = powf((float)vel / 127.0f, 0.6f) * 127.0f + 0.5f;
    if(v < 1.0f)
        v = 1.0f;
    if(v > 127.0f)
        v = 127.0f;
    return (uint8_t)v;
}

inline void CompressVelocities(Track::MidiEv* ev, uint16_t count)
{
    for(uint16_t i = 0; i < count; i++)
    {
        uint8_t type = ev[i].status & 0xF0;
        if(type == 0x90 && ev[i].d2 > 0)
        {
            ev[i].d2 = CompressVelocity(ev[i].d2);
        }
    }
}

// --------------------------------------------------------------------------
// CC automation: canonical table, record thinning, blend math
// --------------------------------------------------------------------------

/**
 * The automatable CCs, addressed everywhere by INDEX into this table.
 * Numbers are the KeyLab Synth-bank controls (cc_map.h) and are what a
 * captured track stores, so playback never depends on the active bank:
 * cutoff, resonance, filter env amount, amp ADSR, synth level.
 */
constexpr uint8_t AUTO_CCS[Track::MAX_AUTO_CC]
    = {74, 71, 79, 93, 18, 19, 16, 85};

/** Index into AUTO_CCS, or -1 if this CC isn't automatable. */
inline int AutoCcIndex(uint8_t cc)
{
    for(int i = 0; i < Track::MAX_AUTO_CC; i++)
    {
        if(AUTO_CCS[i] == cc)
        {
            return i;
        }
    }
    return -1;
}

/** Blend playback: recorded motion + live-knob offset, clamped (SPEC.md). */
inline uint8_t BlendCc(uint8_t recorded, uint8_t live, uint8_t base)
{
    int32_t v = (int32_t)recorded + (int32_t)live - (int32_t)base;
    if(v < 0)
        v = 0;
    if(v > 127)
        v = 127;
    return (uint8_t)v;
}

/**
 * Record-time thinner (v1 Step 13 values): a knob move only lands in the
 * ring if it's >= MIN_INTERVAL ticks since the last recorded point AND
 * moved >= MIN_DELTA. Keeps sweeps at <= 16 points/beat so automation
 * can't crowd notes out of the ring or the 512-event track budget.
 */
struct CcThinner
{
    static constexpr uint32_t MIN_INTERVAL = 6; // ticks
    static constexpr int32_t  MIN_DELTA    = 2;

    uint32_t last_tick[Track::MAX_AUTO_CC];
    uint8_t  last_val[Track::MAX_AUTO_CC];
    uint8_t  seen; // bitmask

    void Reset() { seen = 0; }

    bool ShouldRecord(int idx, uint32_t tick, uint8_t value)
    {
        if(seen & (1u << idx))
        {
            if(tick - last_tick[idx] < MIN_INTERVAL)
            {
                return false;
            }
            int32_t dv = (int32_t)value - (int32_t)last_val[idx];
            if(dv < 0)
                dv = -dv;
            if(dv < MIN_DELTA)
            {
                return false;
            }
        }
        seen |= 1u << idx;
        last_tick[idx] = tick;
        last_val[idx]  = value;
        return true;
    }
};

} // namespace Groove

#endif // GROOVYDAISY_GROOVE_H
