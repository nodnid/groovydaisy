#pragma once
#ifndef GROOVYDAISY_PROTOCOL_H
#define GROOVYDAISY_PROTOCOL_H

#include <stdint.h>
#include <string.h>

/**
 * GroovyDaisy Binary Protocol v2
 *
 * Message format: [SYNC][TYPE][LEN_LO][LEN_HI][PAYLOAD...][CHECKSUM]
 *
 * SYNC:     0xAA - Start of message marker
 * TYPE:     Message type (see below)
 * LEN:      16-bit payload length (little-endian)
 * PAYLOAD:  Variable length data (all multi-byte fields little-endian)
 * CHECKSUM: XOR of all bytes from TYPE through PAYLOAD
 *
 * v2 is EVENT-DRIVEN: state changes push one message; nothing streams at
 * frame rate. The companion interpolates the playhead from MSG_TRANSPORT
 * bpm + MSG_SYNC ticks (sent at ~5 Hz while playing). MSG_MIDI_IN echo is
 * opt-in via CMD_MONITOR. proto_ver in MSG_HELLO is bumped on any layout
 * change; firmware and app ship together, no cross-version compatibility.
 *
 * Daisy -> Companion:
 *   0x01 MSG_HELLO       [proto_ver:1][fw_major:1][fw_minor:1]
 *   0x02 MSG_TRANSPORT   [playing:1][tempo_locked:1][bpm_x10:2]
 *   0x03 MSG_SYNC        [tick:4]  (~5 Hz while playing; also on jumps)
 *   0x04 MSG_VOICES      [synth_active:1][drum_active:1]
 *   0x05 MSG_MIDI_IN     [status:1][data1:1][data2:1]  (opt-in)
 *   0x06 MSG_SYNTH_STATE Full synth params dump (see synth.h layout)
 *   0x07 MSG_BANK        [bank:1]
 *   0x08 MSG_FADER_STATE [9 bytes: pickup flags]
 *   0x09 MSG_MIXER       [strip:1][level:1][pan:1][mute:1][send_rev:1][send_dly:1]
 *   0x0A MSG_METRO       [on:1][level:1]
 *   0x0B MSG_ERROR       [code:1][context:1]
 *   0x0C MSG_CC_STATE    [cc:1][value:1]...
 *   0x10-0x1F            reserved: track/capture family (Phase 2)
 *   0x20-0x2F            reserved: pool/levels/fx (Phases 3/5)
 *   0xFF MSG_DEBUG       [text...]
 *
 * Companion -> Daisy:
 *   0x80 CMD_PLAY        []
 *   0x81 CMD_STOP        []
 *   0x82 CMD_REWIND      []  (double-tap stop is a hardware gesture; the
 *                             app sends CMD_REWIND explicitly)
 *   0x83 CMD_TEMPO       [bpm_x10:2]
 *   0x84 CMD_TAP         []
 *   0x85 CMD_SYNTH_PARAM [param_id:1][value:4 float LE]
 *   0x86 CMD_LOAD_PRESET [preset_index:1]
 *   0x87 CMD_SET_BANK    [bank:1]
 *   0x88 CMD_MIXER       [strip:1][field:1][value:1]
 *   0x89 CMD_METRO       [on:1][level:1]
 *   0x8A CMD_MONITOR     [on:1]
 *   0x90 CMD_REQ_STATE   []  -> full snapshot burst
 *   0xA0-0xAF            reserved: track/capture commands (Phase 2)
 */

namespace Protocol
{

constexpr uint8_t PROTO_VER = 4; // 4: FX + meters (Phase 5)

// Sync byte
constexpr uint8_t SYNC_BYTE = 0xAA;

// Message types: Daisy -> Companion
constexpr uint8_t MSG_HELLO       = 0x01;
constexpr uint8_t MSG_TRANSPORT   = 0x02;
constexpr uint8_t MSG_SYNC        = 0x03;
constexpr uint8_t MSG_VOICES      = 0x04;
constexpr uint8_t MSG_MIDI_IN     = 0x05;
constexpr uint8_t MSG_SYNTH_STATE = 0x06;
constexpr uint8_t MSG_BANK        = 0x07;
constexpr uint8_t MSG_FADER_STATE = 0x08;
constexpr uint8_t MSG_MIXER       = 0x09;
constexpr uint8_t MSG_METRO       = 0x0A;
constexpr uint8_t MSG_ERROR       = 0x0B;
constexpr uint8_t MSG_CC_STATE    = 0x0C;
// Engine-internal mix dump (drum voice levels/pans + synth level/pan/
// master + master out), layout identical to v1 MSG_MIXER_STATE. Sent
// throttled on change and in the snapshot.
constexpr uint8_t MSG_ENGINE_MIX  = 0x0D;

// Track/capture family (Phase 2)
// MSG_TRACK: [slot][gen][kind][len_bars][mute][level][pan][send_rev]
//            [send_dly][created_seq:2] — sent on create and in snapshot
constexpr uint8_t MSG_TRACK       = 0x10;
// MSG_TRACK_GONE: [slot][gen][reason: 0=deleted, 1=undo]
constexpr uint8_t MSG_TRACK_GONE  = 0x11;
// MSG_CAPTURE: [status][source][bars][slot][gen][reason]
constexpr uint8_t MSG_CAPTURE     = 0x12;
// MSG_TRACK_DATA: [slot][gen][chunk_idx][chunk_count][ev0..evN]
//   event = [tick_lo][tick_hi][status][d1][d2] (5 bytes, tick = loop pos)
//   Sent after MSG_TRACK on commit/snapshot; drives arrange-lane rendering
constexpr uint8_t MSG_TRACK_DATA  = 0x13;
// MSG_SRC_ACTIVITY: [pads_bars][pads_active][keys_bars][keys_active]
//   [guitar_bars][guitar_active] — rolling-buffer visibility for the
//   capture history rings, ~4 Hz while playing
constexpr uint8_t MSG_SRC_ACTIVITY = 0x14;
// MSG_AUDIO_PEAKS: [slot][gen][count:2][u8 peaks...] — waveform buckets
//   (24/bar, loop-position order) for audio lanes; sent on commit/snapshot
constexpr uint8_t MSG_AUDIO_PEAKS = 0x15;
// MSG_GROOVE: [quant_pads][quant_keys][swing_pct][vel_comp] — groove
//   settings (Phase 4); sent on change and in the snapshot. quant is
//   0=off 1=light 2=hard; swing_pct is 50..75; vel_comp 0/1.
constexpr uint8_t MSG_GROOVE      = 0x16;
// MSG_POOL: [bars_free:2][bars_total:2] — audio memory gauge; total==0
//   means the pool is unlocked (no audio loops, tempo free)
constexpr uint8_t MSG_POOL        = 0x20;
// MSG_FX: [rev_size][rev_tone][dly_div][dly_fb] — send-FX params
//   (Phase 5); CC scale except dly_div = index into Fx::DIV_16THS.
//   Sent on change and in the snapshot.
constexpr uint8_t MSG_FX          = 0x21;
// MSG_METERS: [master_l][master_r][cpu_pct][36 strip peaks] — 10 Hz.
//   Peaks are sqrt-tapered u8 (mixer.h PeakToCc); the one sanctioned
//   streaming message (SPEC.md Phase 5 meters).
constexpr uint8_t MSG_METERS      = 0x22;
// MSG_STATS: [midi_drops:4][cc_drops:4][tx_lapped_int:4][tx_lapped_ext:4]
//   (all LE, cumulative) — ring-drop diagnostics (Phase 6 giggability).
//   Sent on change, at most 1 Hz. All-zero is the healthy steady state.
constexpr uint8_t MSG_STATS       = 0x23;
constexpr uint8_t MSG_DEBUG       = 0xFF;

// MSG_MIXER/CMD_MIXER strip id for the master bus
constexpr uint8_t MIX_STRIP_MASTER = 0xFF;

// Message types: Companion -> Daisy
constexpr uint8_t CMD_PLAY        = 0x80;
constexpr uint8_t CMD_STOP        = 0x81;
constexpr uint8_t CMD_REWIND      = 0x82;
constexpr uint8_t CMD_TEMPO       = 0x83;
constexpr uint8_t CMD_TAP         = 0x84;
constexpr uint8_t CMD_SYNTH_PARAM = 0x85;
constexpr uint8_t CMD_LOAD_PRESET = 0x86;
constexpr uint8_t CMD_SET_BANK    = 0x87;
constexpr uint8_t CMD_MIXER       = 0x88;
constexpr uint8_t CMD_METRO       = 0x89;
constexpr uint8_t CMD_MONITOR     = 0x8A;
// CMD_MIDI_INJECT: [status][d1][d2] — play the box over USB: injected
// events take the exact same path as UART MIDI (notes -> engines +
// capture rings; CCs -> bank/pickup mapping). The Mac is a band member.
constexpr uint8_t CMD_MIDI_INJECT = 0x8B;
constexpr uint8_t CMD_REQ_STATE   = 0x90;

// Track/capture commands (Phase 2)
constexpr uint8_t CMD_CAPTURE      = 0xA0; // [source (SRC_ANY=all active)][bars (0=source preset)]
constexpr uint8_t CMD_UNDO         = 0xA1; // [] — delete newest capture
constexpr uint8_t CMD_TRACK_DELETE = 0xA2; // [slot][gen] — hold-gesture in UIs
constexpr uint8_t CMD_SRC_LEN      = 0xA3; // [source][bars 1/2/4/8]
constexpr uint8_t CMD_REQ_TRACK_DATA = 0xA5; // [slot][gen]
// CMD_GROOVE: [param][value] — param 0 = quantize pads, 1 = quantize
// keys (0=off 1=light 2=hard), 2 = swing percent (50..75, playback-time,
// live-tweakable), 3 = drum velocity compress (0/1). Echoed as MSG_GROOVE.
constexpr uint8_t CMD_GROOVE       = 0xA6;

// CMD_GROOVE param ids
constexpr uint8_t GROOVE_QUANT_PADS = 0;
constexpr uint8_t GROOVE_QUANT_KEYS = 1;
constexpr uint8_t GROOVE_SWING      = 2;
constexpr uint8_t GROOVE_VEL_COMP   = 3;

// CMD_FX: [param][value] — set one send-FX parameter; echoed as MSG_FX
constexpr uint8_t CMD_FX = 0xA7;

// CMD_FX param ids
constexpr uint8_t FX_REV_SIZE = 0; // reverb feedback, CC scale
constexpr uint8_t FX_REV_TONE = 1; // reverb lowpass, CC scale (log)
constexpr uint8_t FX_DLY_DIV  = 2; // delay division index (0..4)
constexpr uint8_t FX_DLY_FB   = 3; // delay feedback, CC scale

// Capture sources
constexpr uint8_t SRC_PADS   = 0;
constexpr uint8_t SRC_KEYS   = 1;
constexpr uint8_t SRC_GUITAR = 2; // Phase 3
constexpr uint8_t SRC_ANY    = 0xFF;

// MSG_CAPTURE status
constexpr uint8_t CAP_PENDING   = 0;
constexpr uint8_t CAP_COMMITTED = 1;
constexpr uint8_t CAP_REFUSED   = 2;

// MSG_ERROR codes
constexpr uint8_t ERR_TEMPO_LOCKED = 1;
constexpr uint8_t ERR_NOT_PLAYING  = 2;
constexpr uint8_t ERR_POOL_FULL    = 3; // Phase 3
constexpr uint8_t ERR_KIND_CAP     = 4;
constexpr uint8_t ERR_BUSY         = 5; // ring lapped / copy in flight
constexpr uint8_t ERR_NO_HISTORY   = 6; // not enough bars elapsed yet
constexpr uint8_t ERR_EMPTY        = 7; // nothing played in the window

// CMD_MIXER / MSG_MIXER field ids
constexpr uint8_t MIX_FIELD_LEVEL    = 0;
constexpr uint8_t MIX_FIELD_PAN      = 1;
constexpr uint8_t MIX_FIELD_MUTE     = 2;
constexpr uint8_t MIX_FIELD_SEND_REV = 3;
constexpr uint8_t MIX_FIELD_SEND_DLY = 4;

// Maximum payload size
constexpr size_t MAX_PAYLOAD = 256;

// Message buffer (header + max payload + checksum)
constexpr size_t MAX_MESSAGE = 4 + MAX_PAYLOAD + 1;

/**
 * Calculate XOR checksum over a buffer
 */
inline uint8_t Checksum(const uint8_t* data, size_t len)
{
    uint8_t sum = 0;
    for(size_t i = 0; i < len; i++)
    {
        sum ^= data[i];
    }
    return sum;
}

/**
 * Build a message into a buffer
 * Returns total message length (including header and checksum)
 */
inline size_t BuildMessage(uint8_t* buf, uint8_t type,
                           const uint8_t* payload, uint16_t payload_len)
{
    buf[0] = SYNC_BYTE;
    buf[1] = type;
    buf[2] = payload_len & 0xFF;         // LEN low byte
    buf[3] = (payload_len >> 8) & 0xFF;  // LEN high byte

    if(payload_len > 0 && payload != nullptr)
    {
        memcpy(&buf[4], payload, payload_len);
    }

    // Checksum covers TYPE + LEN + PAYLOAD
    uint8_t checksum = Checksum(&buf[1], 3 + payload_len);
    buf[4 + payload_len] = checksum;

    return 5 + payload_len;  // SYNC + TYPE + LEN(2) + PAYLOAD + CHECKSUM
}

/**
 * Parse state for receiving messages
 */
struct Parser
{
    enum State
    {
        WAIT_SYNC,
        WAIT_TYPE,
        WAIT_LEN_LO,
        WAIT_LEN_HI,
        WAIT_PAYLOAD,
        WAIT_CHECKSUM
    };

    State    state;
    uint8_t  type;
    uint16_t payload_len;
    uint16_t payload_idx;
    uint8_t  payload[MAX_PAYLOAD];
    uint8_t  running_checksum;

    void Reset()
    {
        state            = WAIT_SYNC;
        type             = 0;
        payload_len      = 0;
        payload_idx      = 0;
        running_checksum = 0;
    }

    /**
     * Feed a byte to the parser
     * Returns true when a complete valid message is ready
     * Access type and payload[] after returning true
     */
    bool Feed(uint8_t byte)
    {
        switch(state)
        {
            case WAIT_SYNC:
                if(byte == SYNC_BYTE)
                {
                    state            = WAIT_TYPE;
                    running_checksum = 0;
                }
                break;

            case WAIT_TYPE:
                type             = byte;
                running_checksum ^= byte;
                state            = WAIT_LEN_LO;
                break;

            case WAIT_LEN_LO:
                payload_len      = byte;
                running_checksum ^= byte;
                state            = WAIT_LEN_HI;
                break;

            case WAIT_LEN_HI:
                payload_len     |= (uint16_t)byte << 8;
                running_checksum ^= byte;
                payload_idx      = 0;

                if(payload_len > MAX_PAYLOAD)
                {
                    // Invalid length, reset
                    Reset();
                }
                else if(payload_len == 0)
                {
                    state = WAIT_CHECKSUM;
                }
                else
                {
                    state = WAIT_PAYLOAD;
                }
                break;

            case WAIT_PAYLOAD:
                payload[payload_idx++] = byte;
                running_checksum ^= byte;

                if(payload_idx >= payload_len)
                {
                    state = WAIT_CHECKSUM;
                }
                break;

            case WAIT_CHECKSUM:
                if(byte == running_checksum)
                {
                    // Valid message!
                    state = WAIT_SYNC;
                    return true;
                }
                else
                {
                    // Checksum mismatch, reset
                    Reset();
                }
                break;
        }

        return false;
    }
};

} // namespace Protocol

#endif // GROOVYDAISY_PROTOCOL_H
