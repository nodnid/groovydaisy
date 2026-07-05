#pragma once
#ifndef GROOVYDAISY_UI_LINK_H
#define GROOVYDAISY_UI_LINK_H

#include <stdint.h>
#include <string.h>
#include "protocol.h"

/**
 * GroovyDaisy UI publisher (v2).
 *
 * Event-driven state publishing to the companion: state changes push one
 * message; nothing streams at frame rate (v1's 60fps ambitions caused the
 * Step 14 bandwidth failures). The playhead is interpolated client-side
 * from bpm + MSG_SYNC at ~5 Hz.
 *
 * Host-compilable (no libDaisy): the send function is injected. Golden-
 * byte serialization tests live in test/test_ui_link.cpp.
 *
 * Complex payloads that depend on engine headers (MSG_SYNTH_STATE) are
 * serialized by the caller and sent via Send() — this header must stay
 * free of DaisySP-dependent includes.
 */

namespace UiLink
{

constexpr uint8_t FW_MAJOR = 2;
constexpr uint8_t FW_MINOR = 0;

class Publisher
{
  public:
    typedef void (*SendFn)(const uint8_t* data, size_t len);

    void Init(SendFn fn)
    {
        send_            = fn;
        monitor_enabled_ = false;
    }

    void SetMonitor(bool on) { monitor_enabled_ = on; }
    bool MonitorEnabled() const { return monitor_enabled_; }

    void Send(uint8_t type, const uint8_t* payload, uint16_t len)
    {
        size_t n = Protocol::BuildMessage(tx_, type, payload, len);
        send_(tx_, n);
    }

    void Hello()
    {
        uint8_t p[3] = {Protocol::PROTO_VER, FW_MAJOR, FW_MINOR};
        Send(Protocol::MSG_HELLO, p, 3);
    }

    void Transport(bool playing, bool tempo_locked, float bpm, bool preroll)
    {
        uint16_t bpm_x10 = (uint16_t)(bpm * 10.0f + 0.5f);
        uint8_t  p[5];
        p[0] = playing ? 1 : 0;
        p[1] = tempo_locked ? 1 : 0;
        p[2] = bpm_x10 & 0xFF;
        p[3] = (bpm_x10 >> 8) & 0xFF;
        p[4] = preroll ? 1 : 0; // count-in bar in progress
        Send(Protocol::MSG_TRANSPORT, p, 5);
    }

    void Sync(uint32_t tick)
    {
        uint8_t p[4];
        p[0] = tick & 0xFF;
        p[1] = (tick >> 8) & 0xFF;
        p[2] = (tick >> 16) & 0xFF;
        p[3] = (tick >> 24) & 0xFF;
        Send(Protocol::MSG_SYNC, p, 4);
    }

    void Voices(uint8_t synth_active, uint8_t drum_active)
    {
        uint8_t p[2] = {synth_active, drum_active};
        Send(Protocol::MSG_VOICES, p, 2);
    }

    /** Caller gates on MonitorEnabled() — echo is opt-in by design. */
    void MidiIn(uint8_t status, uint8_t d1, uint8_t d2)
    {
        uint8_t p[3] = {status, d1, d2};
        Send(Protocol::MSG_MIDI_IN, p, 3);
    }

    void Bank(uint8_t bank) { Send(Protocol::MSG_BANK, &bank, 1); }

    void FaderState(const uint8_t* flags, uint8_t count)
    {
        Send(Protocol::MSG_FADER_STATE, flags, count);
    }

    void MixerStrip(uint8_t strip, uint8_t level, uint8_t pan, bool mute,
                    uint8_t send_rev, uint8_t send_dly)
    {
        uint8_t p[6] = {strip, level, pan, (uint8_t)(mute ? 1 : 0),
                        send_rev, send_dly};
        Send(Protocol::MSG_MIXER, p, 6);
    }

    void Metro(bool on, uint8_t level)
    {
        uint8_t p[2] = {(uint8_t)(on ? 1 : 0), level};
        Send(Protocol::MSG_METRO, p, 2);
    }

    void Error(uint8_t code, uint8_t context)
    {
        uint8_t p[2] = {code, context};
        Send(Protocol::MSG_ERROR, p, 2);
    }

    void Debug(const char* text)
    {
        size_t len = strlen(text);
        if(len > Protocol::MAX_PAYLOAD)
            len = Protocol::MAX_PAYLOAD;
        Send(Protocol::MSG_DEBUG, (const uint8_t*)text, (uint16_t)len);
    }

  private:
    SendFn  send_;
    bool    monitor_enabled_;
    uint8_t tx_[Protocol::MAX_MESSAGE];
};

} // namespace UiLink

#endif // GROOVYDAISY_UI_LINK_H
