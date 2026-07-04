#pragma once
#ifndef GROOVYDAISY_MIDI_ROUTER_H
#define GROOVYDAISY_MIDI_ROUTER_H

#include <stdint.h>
#include "sampler.h"
#include "synth.h"
#include "cc_map.h"

/**
 * GroovyDaisy Unified MIDI Router
 *
 * Centralizes all MIDI event routing so that:
 * - Live MIDI and sequencer playback go through the same path
 * - All events can be forwarded to companion app (MIDI Monitor)
 * - Easy to add MIDI output for external gear later
 */

namespace MidiRouter
{

// Event source (for diagnostics/filtering)
enum class Source : uint8_t
{
    LIVE_INPUT,    // From UART MIDI (KeyLab)
    SEQUENCER,     // From sequencer playback
};

// Callback type for companion notification (SendMidiIn)
typedef void (*MidiOutCallback)(uint8_t status, uint8_t data1, uint8_t data2);

// Callback type for recording to sequencer
typedef void (*RecordCallback)(uint32_t tick, uint8_t status, uint8_t data1, uint8_t data2);

/**
 * Unified MIDI Router
 *
 * Routes MIDI events to appropriate destinations:
 * - Sampler (drum notes on channel 10)
 * - Synth (notes/CCs on channel 1)
 * - Companion app (all events for MIDI Monitor)
 */
class Router
{
  public:
    void Init(Sampler::Engine* sampler,
              Synth::Engine* synth,
              MidiOutCallback companion_cb)
    {
        sampler_ = sampler;
        synth_ = synth;
        companion_cb_ = companion_cb;
        record_cb_ = nullptr;
    }

    /**
     * Set callback for recording events to sequencer
     */
    void SetRecordCallback(RecordCallback cb) { record_cb_ = cb; }

    /**
     * Route a Note On event
     *
     * @param channel MIDI channel (0-15)
     * @param note Note number (0-127)
     * @param velocity Velocity (0-127, 0 = note off)
     * @param source Where this event came from
     * @param record If true, record to sequencer
     * @param tick Current transport tick (for recording)
     */
    void RouteNoteOn(uint8_t channel, uint8_t note, uint8_t velocity,
                     Source source, bool record = false, uint32_t tick = 0)
    {
        // Handle velocity 0 as note off (per MIDI spec)
        if(velocity == 0)
        {
            RouteNoteOff(channel, note, source);
            return;
        }

        // Route to sampler (drum channel)
        if(channel == Sampler::DRUM_CHANNEL)
        {
            sampler_->TriggerMidi(channel, note, velocity);
        }

        // Route to synth (synth channel)
        if(channel == Synth::SYNTH_CHANNEL)
        {
            synth_->NoteOn(note, velocity);
        }

        // Forward to companion (MIDI Monitor) - only for live input
        // Sequencer events are queued separately to avoid audio callback USB calls
        if(source == Source::LIVE_INPUT && companion_cb_ != nullptr)
        {
            uint8_t status = 0x90 | (channel & 0x0F);
            companion_cb_(status, note, velocity);
        }

        // Record if enabled
        if(record && record_cb_ != nullptr)
        {
            uint8_t status = 0x90 | (channel & 0x0F);
            record_cb_(tick, status, note, velocity);
        }
    }

    /**
     * Route a Note Off event
     */
    void RouteNoteOff(uint8_t channel, uint8_t note, Source source,
                      bool record = false, uint32_t tick = 0)
    {
        // Sampler doesn't need NoteOff (one-shot samples)

        // Route to synth (synth channel)
        if(channel == Synth::SYNTH_CHANNEL)
        {
            synth_->NoteOff(note);
        }

        // Forward to companion (MIDI Monitor)
        if(source == Source::LIVE_INPUT && companion_cb_ != nullptr)
        {
            uint8_t status = 0x80 | (channel & 0x0F);
            companion_cb_(status, note, 0);
        }

        // Record NoteOff for synth (needed for proper playback)
        if(record && record_cb_ != nullptr && channel == Synth::SYNTH_CHANNEL)
        {
            uint8_t status = 0x80 | (channel & 0x0F);
            record_cb_(tick, status, note, 0);
        }
    }

    /**
     * Route a Control Change event
     */
    void RouteCC(uint8_t channel, uint8_t cc, uint8_t value, Source source)
    {
        // Route synth CCs
        if(channel == Synth::SYNTH_CHANNEL)
        {
            CCMap::HandleSynthCC(cc, value, *synth_);
        }

        // Forward to companion (MIDI Monitor)
        if(source == Source::LIVE_INPUT && companion_cb_ != nullptr)
        {
            uint8_t status = 0xB0 | (channel & 0x0F);
            companion_cb_(status, cc, value);
        }
    }

  private:
    Sampler::Engine* sampler_;
    Synth::Engine* synth_;
    MidiOutCallback companion_cb_;
    RecordCallback record_cb_;
};

} // namespace MidiRouter

#endif // GROOVYDAISY_MIDI_ROUTER_H
