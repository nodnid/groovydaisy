#pragma once
#ifndef GROOVYDAISY_TRACK_EDIT_H
#define GROOVYDAISY_TRACK_EDIT_H

#include <stdint.h>
#include "track.h"

/**
 * GroovyDaisy in-place track editing (horizon #1: note editing on lanes —
 * the groovebox pull).
 *
 * Pure event-array surgery, host-testable. main.cpp wraps each edit in
 * the never-mutate-active-payload dance: deactivate the slot (callback
 * releases its notes and stops reading the payload — single core, the
 * audio ISR observes the store before any mutation below can run), apply
 * the edit, bump gen, reactivate, republish MSG_TRACK + MSG_TRACK_DATA.
 *
 * Ops mirror what the lanes can express:
 * - ToggleDrum: click a drum-grid cell — remove the hit if one exists at
 *   exactly (tick, note), else add one. The UI sends the exact tick of an
 *   existing dot for removal and the grid-snapped tick for adds.
 * - DeleteNote: remove a synth note-on at (tick, note) and its matched
 *   note-off (wrap-aware pairing across the loop seam).
 * - MoveNote: shift a synth note (on + matched off) to a new tick and/or
 *   pitch — the piano-roll drag. Duration is preserved mod loop length.
 *
 * Arrays stay tick-sorted after every op (insertion resort, stable).
 */

namespace TrackEdit
{

enum class Result : uint8_t
{
    Added = 0,
    Removed,
    Moved,
    NotFound,
    Full,
};

inline void SortEvents(Track::MidiEv* ev, uint16_t count)
{
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

inline bool IsOn(const Track::MidiEv& e)
{
    return (e.status & 0xF0) == 0x90 && e.d2 > 0;
}

inline bool IsOff(const Track::MidiEv& e)
{
    uint8_t t = e.status & 0xF0;
    return t == 0x80 || (t == 0x90 && e.d2 == 0);
}

/** Index of the note-on at exactly (tick, note), or -1. */
inline int FindOn(const Track::MidiEv* ev, uint16_t count, uint32_t tick,
                  uint8_t note)
{
    for(uint16_t i = 0; i < count; i++)
    {
        if(ev[i].tick == tick && ev[i].d1 == note && IsOn(ev[i]))
        {
            return i;
        }
    }
    return -1;
}

/**
 * Index of the note-off matching the on at index `on_idx` — the first
 * off for that note scanning forward CIRCULARLY from the on (a note
 * sustaining across the loop seam has its off earlier in the array).
 * Returns -1 if the note has no off (drum hits).
 */
inline int FindMatchingOff(const Track::MidiEv* ev, uint16_t count,
                           int on_idx)
{
    uint8_t note = ev[on_idx].d1;
    for(uint16_t k = 1; k < count; k++)
    {
        uint16_t i = (uint16_t)((on_idx + k) % count);
        if(ev[i].d1 == note && IsOff(ev[i]))
        {
            return i;
        }
        if(ev[i].d1 == note && IsOn(ev[i]))
        {
            return -1; // hit the note's next on first: no off for ours
        }
    }
    return -1;
}

/** Remove the event at index `idx` (shifts the tail down). */
inline void RemoveAt(Track::MidiEv* ev, uint16_t& count, int idx)
{
    for(uint16_t i = (uint16_t)idx; i + 1 < count; i++)
    {
        ev[i] = ev[i + 1];
    }
    count--;
}

/**
 * Toggle a drum hit at exactly (tick, note). Drums are one-shots: no
 * off bookkeeping. `channel` is the drum channel (9).
 */
inline Result ToggleDrum(Track::MidiEv* ev, uint16_t& count,
                         uint16_t max_events, uint32_t tick, uint8_t note,
                         uint8_t vel)
{
    int idx = FindOn(ev, count, tick, note);
    if(idx >= 0)
    {
        RemoveAt(ev, count, idx);
        return Result::Removed;
    }
    if(count >= max_events)
    {
        return Result::Full;
    }
    ev[count++] = {tick, (uint8_t)0x99, note, vel};
    SortEvents(ev, count);
    return Result::Added;
}

/** Delete a synth note: the on at (tick, note) and its matched off. */
inline Result DeleteNote(Track::MidiEv* ev, uint16_t& count, uint32_t tick,
                         uint8_t note)
{
    int on = FindOn(ev, count, tick, note);
    if(on < 0)
    {
        return Result::NotFound;
    }
    int off = FindMatchingOff(ev, count, on);
    // Remove the higher index first so the lower stays valid
    if(off > on)
    {
        RemoveAt(ev, count, off);
        RemoveAt(ev, count, on);
    }
    else if(off >= 0)
    {
        RemoveAt(ev, count, on);
        RemoveAt(ev, count, off);
    }
    else
    {
        RemoveAt(ev, count, on);
    }
    return Result::Removed;
}

/**
 * Move a synth note to (new_tick, new_note); its off keeps the same
 * duration (delta applied mod loop length). The piano-roll drag.
 */
inline Result MoveNote(Track::MidiEv* ev, uint16_t& count,
                       uint32_t len_ticks, uint32_t tick, uint8_t note,
                       uint32_t new_tick, uint8_t new_note)
{
    if(new_tick >= len_ticks || new_note > 127)
    {
        return Result::NotFound;
    }
    int on = FindOn(ev, count, tick, note);
    if(on < 0)
    {
        return Result::NotFound;
    }
    int off = FindMatchingOff(ev, count, on);

    // Delta the whole note travels (wrapped): same rule as quantize
    int64_t delta = (int64_t)new_tick - (int64_t)tick;

    ev[on].tick = new_tick;
    ev[on].d1   = new_note;
    if(off >= 0)
    {
        int64_t t = (int64_t)ev[off].tick + delta;
        t %= (int64_t)len_ticks;
        if(t < 0)
            t += len_ticks;
        ev[off].tick = (uint32_t)t;
        ev[off].d1   = new_note;
    }
    SortEvents(ev, count);
    return Result::Moved;
}

} // namespace TrackEdit

#endif // GROOVYDAISY_TRACK_EDIT_H
