#pragma once
#ifndef GROOVYDAISY_PROTOCOL_H
#define GROOVYDAISY_PROTOCOL_H

#include <stdint.h>
#include <string.h>

/**
 * GroovyDaisy Binary Protocol
 *
 * Message format: [SYNC][TYPE][LEN_LO][LEN_HI][PAYLOAD...][CHECKSUM]
 *
 * SYNC:     0xAA - Start of message marker
 * TYPE:     Message type (see below)
 * LEN:      16-bit payload length (little-endian)
 * PAYLOAD:  Variable length data
 * CHECKSUM: XOR of all bytes from TYPE through PAYLOAD
 *
 * Daisy -> Companion (state updates):
 *   0x01 MSG_TICK      - Playhead position [tick:4]
 *   0x02 MSG_TRANSPORT - Transport state [playing:1][recording:1][bpm:2]
 *   0x03 MSG_VOICES    - Voice activity [synth:1][drums:1]
 *   0x04 MSG_MIDI_IN   - MIDI event received [status:1][data1:1][data2:1]
 *   0x05 MSG_CC_STATE  - CC values [cc:1][value:1]...
 *   0x06 MSG_SYNTH_STATE - Full synth params dump (see SynthParams struct)
 *   0x07 MSG_CC_BANK   - Current CC bank [bank:1]
 *   0x08 MSG_FADER_STATE - Fader pickup states [9 bytes: picked_up flags]
 *   0x09 MSG_MIXER_STATE - Mixer state (levels/pans, see below)
 *   0xFF MSG_DEBUG     - Debug text [string...]
 *
 * MSG_MIXER_STATE payload:
 *   [drum_levels:8][drum_pans:8][drum_master:1][synth_level:1][synth_pan:1][synth_master:1][master_out:1]
 *   All values 0-127 (CC scale). Pan: 0=full left, 64=center, 127=full right.
 *
 * Companion -> Daisy (commands):
 *   0x80 CMD_PLAY      - Start playback []
 *   0x81 CMD_STOP      - Stop playback []
 *   0x82 CMD_RECORD    - Toggle record []
 *   0x83 CMD_TEMPO     - Set tempo [bpm:2]
 *   0x84 CMD_PATTERN   - Select pattern [num:1]
 *   0x85 CMD_SYNTH_PARAM - Set synth param [param_id:1][value:4 float LE]
 *   0x86 CMD_LOAD_PRESET - Load preset [preset_index:1]
 *   0x87 CMD_SET_BANK  - Set CC bank [bank:1]
 *   0x90 CMD_REQ_STATE - Request full state dump []
 *   0x92 CMD_REQ_SYNTH - Request synth state []
 */

namespace Protocol
{

// Sync byte
constexpr uint8_t SYNC_BYTE = 0xAA;

// Message types: Daisy -> Companion
constexpr uint8_t MSG_TICK        = 0x01;
constexpr uint8_t MSG_TRANSPORT   = 0x02;
constexpr uint8_t MSG_VOICES      = 0x03;
constexpr uint8_t MSG_MIDI_IN     = 0x04;
constexpr uint8_t MSG_CC_STATE    = 0x05;
constexpr uint8_t MSG_SYNTH_STATE = 0x06;
constexpr uint8_t MSG_CC_BANK     = 0x07;
constexpr uint8_t MSG_FADER_STATE = 0x08;
constexpr uint8_t MSG_MIXER_STATE = 0x09;
constexpr uint8_t MSG_DEBUG       = 0xFF;

// Message types: Companion -> Daisy
constexpr uint8_t CMD_PLAY        = 0x80;
constexpr uint8_t CMD_STOP        = 0x81;
constexpr uint8_t CMD_RECORD      = 0x82;
constexpr uint8_t CMD_TEMPO       = 0x83;
constexpr uint8_t CMD_PATTERN     = 0x84;
constexpr uint8_t CMD_SYNTH_PARAM = 0x85;
constexpr uint8_t CMD_LOAD_PRESET = 0x86;
constexpr uint8_t CMD_SET_BANK    = 0x87;
constexpr uint8_t CMD_REQ_STATE   = 0x90;
constexpr uint8_t CMD_REQ_SYNTH   = 0x92;

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
