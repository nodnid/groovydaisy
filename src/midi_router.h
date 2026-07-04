#pragma once
#ifndef GROOVYDAISY_MIDI_ROUTER_H
#define GROOVYDAISY_MIDI_ROUTER_H

#include <stdint.h>
#include "sampler.h"
#include "synth.h"

/**
 * GroovyDaisy MIDI router (v2).
 *
 * THE single dispatch path for note events: live input (drained from the
 * midi_to_audio ring inside the audio callback) and sequenced playback
 * (Phase 2) both land here, so engines can't drift apart on behavior.
 *
 * Callback-safe: no allocation, no USB. CC events do NOT come through
 * here — they carry bank/pickup logic and stay in the main loop
 * (cc_map.h), applying engine parameters directly.
 *
 * Channel map: ch 1 (0-indexed 0) -> synth keys; ch 10 (0-indexed 9)
 * notes 36-43 -> drum pads.
 */

namespace MidiRouter
{

constexpr uint8_t SYNTH_CHANNEL = 0;
constexpr uint8_t DRUM_CHANNEL  = 9;

enum class Source : uint8_t
{
    Live,
    Playback // sequenced (Phase 2)
};

struct Event
{
    uint8_t status; // status byte incl. channel
    uint8_t d1;
    uint8_t d2;
};

class Router
{
  public:
    /** Called for every LIVE event dispatched — Phase 2 wires the
     *  capture rings here. May be null. Callback context! */
    typedef void (*RecordHook)(const Event& e, uint32_t tick);

    void Init(Sampler::Engine* sampler, Synth::Engine* synth)
    {
        sampler_ = sampler;
        synth_   = synth;
        record_  = nullptr;
    }

    void SetRecordHook(RecordHook hook) { record_ = hook; }

    /**
     * Dispatch one note event to the engines.
     * vel_scale: MIDI-track strip level (1.0 for live input).
     * Runs in the audio callback — keep it lean.
     */
    void Dispatch(const Event& e, Source src, uint32_t tick,
                  float vel_scale = 1.0f)
    {
        uint8_t channel = e.status & 0x0F;
        uint8_t type    = e.status & 0xF0;

        bool is_on  = (type == 0x90 && e.d2 > 0);
        bool is_off = (type == 0x80 || (type == 0x90 && e.d2 == 0));

        if(!is_on && !is_off)
        {
            return;
        }

        if(src == Source::Live && record_ != nullptr)
        {
            record_(e, tick);
        }

        if(channel == DRUM_CHANNEL)
        {
            if(is_on)
            {
                uint8_t vel = ScaleVel(e.d2, vel_scale);
                sampler_->TriggerMidi(channel, e.d1, vel);
            }
            // drums are one-shots: note-off ignored
        }
        else if(channel == SYNTH_CHANNEL)
        {
            if(is_on)
            {
                synth_->NoteOn(e.d1, ScaleVel(e.d2, vel_scale));
            }
            else
            {
                synth_->NoteOff(e.d1);
            }
        }
    }

  private:
    static uint8_t ScaleVel(uint8_t vel, float scale)
    {
        float v = (float)vel * scale;
        if(v < 1.0f)
            v = 1.0f; // scaled note-on must stay a note-on
        if(v > 127.0f)
            v = 127.0f;
        return (uint8_t)v;
    }

    Sampler::Engine* sampler_;
    Synth::Engine*   synth_;
    RecordHook       record_;
};

} // namespace MidiRouter

#endif // GROOVYDAISY_MIDI_ROUTER_H
