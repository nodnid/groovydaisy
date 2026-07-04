#pragma once
#ifndef GROOVYDAISY_SEQ_TRACK_H
#define GROOVYDAISY_SEQ_TRACK_H

#include <stdint.h>
#include "track.h"
#include "mixer.h"

/**
 * MIDI loop-track playback (v2).
 *
 * Runs per tick inside the audio callback. Every active MIDI track plays
 * its events where `global_tick % length == event.tick` — same-length
 * tracks are inherently phase-aligned (SPEC.md track model).
 *
 * Strip semantics for MIDI tracks (SPEC decision 7): level scales note-on
 * velocity, mute suppresses dispatch. Mute/delete transitions release the
 * track's sounding synth notes so nothing hangs.
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
 */
inline void ProcessTick(Track::Registry& reg, Mixer::Engine& mixer,
                        uint32_t global_tick, DispatchFn dispatch)
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

        // Reposition after a jump/wrap (rewind, first tick, loop seam)
        if(s.last_tick_mod == 0xFFFFFFFF || t != (s.last_tick_mod + 1) % len)
        {
            uint16_t idx = 0;
            while(idx < s.event_count && s.events[idx].tick < t)
            {
                idx++;
            }
            s.playback_idx = idx;
        }
        s.last_tick_mod = t;

        // Dispatch everything scheduled at this position
        while(s.playback_idx < s.event_count
              && s.events[s.playback_idx].tick == t)
        {
            const Track::MidiEv& ev = s.events[s.playback_idx];

            uint8_t type   = ev.status & 0xF0;
            bool    is_on  = type == 0x90 && ev.d2 > 0;
            bool    is_off = type == 0x80 || (type == 0x90 && ev.d2 == 0);

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
