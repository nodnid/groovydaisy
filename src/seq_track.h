#pragma once
#ifndef GROOVYDAISY_SEQ_TRACK_H
#define GROOVYDAISY_SEQ_TRACK_H

#include <stdint.h>
#include "track.h"
#include "mixer.h"
#include "groove.h"

/**
 * MIDI loop-track playback (v2).
 *
 * Runs per tick inside the audio callback. Every active MIDI track plays
 * its events where `global_tick % length == SwingWarp(event.tick)` —
 * same-length tracks are inherently phase-aligned (SPEC.md track model),
 * and swing (Phase 4) is a playback-time warp of the comparison, never a
 * mutation of stored events: non-destructive and live-tweakable. The warp
 * is monotonic, so the sorted event array stays dispatch-ordered; a
 * mid-bar swing change can shift an event behind the playhead, which the
 * `<=` dispatch catches late rather than dropping (note-offs must land).
 *
 * Strip semantics for MIDI tracks (SPEC decision 7): level scales note-on
 * velocity, mute suppresses dispatch. Mute/delete transitions release the
 * track's sounding synth notes so nothing hangs.
 *
 * CC events (0xB0, Phase 4 automation) dispatch with the blend applied:
 * effective = recorded + (live_cc - cc_base), so live knob turns ride on
 * top of recorded motion (SPEC.md CC automation).
 *
 * Host-testable: dispatch is injected as a callback, so this header pulls
 * in no engine/DaisySP dependencies.
 */

namespace SeqTrack
{

/** (status, d1, d2, vel_scale) — bound to midi_router in main.cpp. */
typedef void (*DispatchFn)(uint8_t status, uint8_t d1, uint8_t d2,
                           float vel_scale);

/** Release every sounding note this track has (mute/delete/stop). */
inline void ReleaseNotes(Track::Slot& s, DispatchFn dispatch)
{
    for(uint8_t note = 0; note < 128; note++)
    {
        if(s.notes_on[note >> 5] & (1u << (note & 31)))
        {
            dispatch(0x80, note, 0, 1.0f);
        }
    }
    for(int b = 0; b < 4; b++)
        s.notes_on[b] = 0;
}

/**
 * Advance one tick of playback across all MIDI tracks.
 * Audio-callback context — no allocation, bounded work.
 *
 * swing_ticks: 0 (straight) .. Groove::MAX_SWING_TICKS (75% shuffle).
 * live_cc: current knob values in Groove::AUTO_CCS order (may be null:
 * CC events then play back unblended).
 */
inline void ProcessTick(Track::Registry& reg, Mixer::Engine& mixer,
                        uint32_t global_tick, DispatchFn dispatch,
                        uint8_t swing_ticks = 0,
                        const uint8_t* live_cc = nullptr)
{
    for(int i = 0; i < Track::MAX_TRACKS; i++)
    {
        Track::Slot& s = reg.Get(i);

        bool active = s.active.load(std::memory_order_acquire);

        // Deactivated with notes still sounding (delete/undo mid-note)
        if(!active)
        {
            bool any = s.notes_on[0] | s.notes_on[1] | s.notes_on[2]
                       | s.notes_on[3];
            if(any)
            {
                ReleaseNotes(s, dispatch);
            }
            continue;
        }
        if(s.kind == Track::Kind::Audio)
        {
            continue; // audio playback is per-sample, not per-tick (Phase 3)
        }

        const Mixer::Strip& strip = mixer.Get(i);

        // Mute transition: release, then stay silent while muted
        if(strip.mute)
        {
            if(!s.prev_mute)
            {
                ReleaseNotes(s, dispatch);
                s.prev_mute = true;
            }
            continue;
        }
        s.prev_mute = false;

        const uint32_t len = s.LengthTicks();
        const uint32_t t   = global_tick % len;

        // Reposition after a jump/wrap (rewind, first tick, loop seam) —
        // comparisons run on SWUNG positions (monotonic warp keeps the
        // sorted array dispatch-ordered)
        if(s.last_tick_mod == 0xFFFFFFFF || t != (s.last_tick_mod + 1) % len)
        {
            uint16_t idx = 0;
            while(idx < s.event_count
                  && Groove::SwingWarp(s.events[idx].tick, swing_ticks) < t)
            {
                idx++;
            }
            s.playback_idx = idx;
        }
        s.last_tick_mod = t;

        // Dispatch everything scheduled at (or, after a live swing tweak,
        // just behind) this position
        while(s.playback_idx < s.event_count
              && Groove::SwingWarp(s.events[s.playback_idx].tick, swing_ticks)
                     <= t)
        {
            const Track::MidiEv& ev = s.events[s.playback_idx];

            uint8_t type   = ev.status & 0xF0;
            bool    is_on  = type == 0x90 && ev.d2 > 0;
            bool    is_off = type == 0x80 || (type == 0x90 && ev.d2 == 0);

            if(type == 0xB0) // CC automation: blend live offset on top
            {
                uint8_t val = ev.d2;
                int     a   = Groove::AutoCcIndex(ev.d1);
                if(a >= 0 && live_cc != nullptr)
                {
                    val = Groove::BlendCc(ev.d2, live_cc[a], s.cc_base[a]);
                }
                dispatch(ev.status, ev.d1, val, strip.gain);
                s.playback_idx++;
                continue;
            }

            if(s.kind == Track::Kind::MidiSynth)
            {
                if(is_on)
                    s.notes_on[ev.d1 >> 5] |= 1u << (ev.d1 & 31);
                else if(is_off)
                    s.notes_on[ev.d1 >> 5] &= ~(1u << (ev.d1 & 31));
            }

            dispatch(ev.status, ev.d1, ev.d2, strip.gain);
            s.playback_idx++;
        }

        // Wrap the index at loop end so the next cycle starts clean
        if(s.playback_idx >= s.event_count && t == len - 1)
        {
            s.playback_idx = 0;
        }
    }
}

} // namespace SeqTrack

#endif // GROOVYDAISY_SEQ_TRACK_H
