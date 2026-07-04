/**
 * GroovyDaisy - Step 11/12: Polysynth Engine + CC Control
 *
 * Features:
 * - Transport engine with PPQN-based timing
 * - Play/Stop/Record state controllable from companion
 * - Tempo control with BPM display
 * - Position tracking (bar:beat:tick)
 * - MIDI input from KeyLab via UART, forwarded to companion
 * - Pod buttons: Button1=Play/Stop, Button2=Record toggle
 * - Encoder: Adjust tempo
 * - 8-voice sample drum engine triggered by KeyLab pads (notes 36-43)
 * - 6-voice polysynth triggered by KeyLab keys (channel 1)
 * - CC control of synth parameters via KeyLab encoders/faders
 * - Factory presets and companion app parameter control
 *
 * Message format: [0xAA][TYPE][LEN_LO][LEN_HI][PAYLOAD...][CHECKSUM]
 */

#include <stdio.h>
#include <string.h>
#include "daisy_pod.h"
#include "daisysp.h"
#include "protocol.h"
#include "transport.h"
#include "sampler.h"
#include "sequencer.h"
#include "synth.h"
#include "cc_map.h"
#include "midi_router.h"
#include "automation.h"
#include "samples/drums.h"
#include "util/CpuLoadMeter.h"

using namespace daisy;
using namespace daisysp;

DaisyPod hw;
Transport::Engine transport;
Sampler::Engine sampler;
Sequencer::Engine sequencer;
Synth::Engine synth;
Automation::Engine automation;
MidiRouter::Router midi_router;
CCMap::Engine cc_engine;

// Sample bank in SDRAM (must be at global scope with DSY_SDRAM_BSS)
DrumSamples::SampleBank DSY_SDRAM_BSS sample_bank;

// CPU load meter for diagnostics
CpuLoadMeter cpu_meter;

// Playback queue for sequencer events -> main loop (for MIDI Monitor)
// Audio callback queues events, main loop sends to companion
struct PlaybackEvent { uint8_t status; uint8_t data1; uint8_t data2; };
constexpr size_t PLAYBACK_QUEUE_SIZE = 64;
PlaybackEvent playback_queue[PLAYBACK_QUEUE_SIZE];
volatile uint8_t playback_queue_head = 0;
volatile uint8_t playback_queue_tail = 0;

// Debug counters for tracking playback events
volatile uint32_t debug_noteon_queued = 0;
volatile uint32_t debug_noteoff_queued = 0;
volatile uint32_t debug_queue_full = 0;

// Flag to send transport state update
static volatile bool send_transport_update = false;

// Voice count tracking for MSG_VOICES
static volatile uint8_t last_synth_count = 0;
static volatile uint8_t last_drum_count = 0;
static volatile bool send_voices_update = false;

/**
 * Record callback - called by router to record events to sequencer
 */
void RouterRecordCallback(uint32_t tick, uint8_t status, uint8_t data1, uint8_t data2)
{
    sequencer.RecordEvent(tick, status, data1, data2);
}

/**
 * Apply a CC value to a parameter target
 * Routes to appropriate engine (synth, sampler, etc.)
 */
void ApplyParamTarget(CCMap::ParamTarget target, uint8_t cc_value)
{
    using namespace CCMap;

    switch(target)
    {
        // Synth parameters
        case TARGET_SYNTH_OSC1_WAVE:
            synth.SetParam(Synth::PARAM_OSC1_WAVE, CCToWave(cc_value));
            break;
        case TARGET_SYNTH_OSC2_WAVE:
            synth.SetParam(Synth::PARAM_OSC2_WAVE, CCToWave(cc_value));
            break;
        case TARGET_SYNTH_OSC1_LEVEL:
            synth.SetParam(Synth::PARAM_OSC1_LEVEL, CCToNorm(cc_value));
            break;
        case TARGET_SYNTH_OSC2_LEVEL:
            synth.SetParam(Synth::PARAM_OSC2_LEVEL, CCToNorm(cc_value));
            break;
        case TARGET_SYNTH_OSC2_DETUNE:
            synth.SetParam(Synth::PARAM_OSC2_DETUNE, CCToSemitones(cc_value));
            break;
        case TARGET_SYNTH_FILTER_CUTOFF:
            synth.SetParam(Synth::PARAM_FILTER_CUTOFF, CCToFreq(cc_value));
            break;
        case TARGET_SYNTH_FILTER_RES:
            synth.SetParam(Synth::PARAM_FILTER_RES, CCToNorm(cc_value));
            break;
        case TARGET_SYNTH_FILTER_ENV_AMT:
            synth.SetParam(Synth::PARAM_FILTER_ENV_AMT, CCToNorm(cc_value));
            break;
        case TARGET_SYNTH_AMP_ATTACK:
            synth.SetParam(Synth::PARAM_AMP_ATTACK, CCToTime(cc_value));
            break;
        case TARGET_SYNTH_AMP_DECAY:
            synth.SetParam(Synth::PARAM_AMP_DECAY, CCToTime(cc_value));
            break;
        case TARGET_SYNTH_AMP_SUSTAIN:
            synth.SetParam(Synth::PARAM_AMP_SUSTAIN, CCToNorm(cc_value));
            break;
        case TARGET_SYNTH_AMP_RELEASE:
            synth.SetParam(Synth::PARAM_AMP_RELEASE, CCToTime(cc_value));
            break;
        case TARGET_SYNTH_FILT_ATTACK:
            synth.SetParam(Synth::PARAM_FILT_ATTACK, CCToTime(cc_value));
            break;
        case TARGET_SYNTH_FILT_DECAY:
            synth.SetParam(Synth::PARAM_FILT_DECAY, CCToTime(cc_value));
            break;
        case TARGET_SYNTH_FILT_SUSTAIN:
            synth.SetParam(Synth::PARAM_FILT_SUSTAIN, CCToNorm(cc_value));
            break;
        case TARGET_SYNTH_FILT_RELEASE:
            synth.SetParam(Synth::PARAM_FILT_RELEASE, CCToTime(cc_value));
            break;
        case TARGET_SYNTH_VEL_TO_AMP:
            synth.SetParam(Synth::PARAM_VEL_TO_AMP, CCToNorm(cc_value));
            break;
        case TARGET_SYNTH_VEL_TO_FILTER:
            synth.SetParam(Synth::PARAM_VEL_TO_FILTER, CCToNorm(cc_value));
            break;
        case TARGET_SYNTH_LEVEL:
            synth.SetParam(Synth::PARAM_LEVEL, CCToNorm(cc_value));
            break;
        case TARGET_SYNTH_PAN:
            synth.SetParam(Synth::PARAM_PAN, CCToPan(cc_value));
            break;
        case TARGET_SYNTH_MASTER_LEVEL:
            synth.SetParam(Synth::PARAM_MASTER_LEVEL, CCToNorm(cc_value));
            break;

        // Drum levels
        case TARGET_DRUM_1_LEVEL:
            sampler.SetLevel(0, CCToNorm(cc_value));
            break;
        case TARGET_DRUM_2_LEVEL:
            sampler.SetLevel(1, CCToNorm(cc_value));
            break;
        case TARGET_DRUM_3_LEVEL:
            sampler.SetLevel(2, CCToNorm(cc_value));
            break;
        case TARGET_DRUM_4_LEVEL:
            sampler.SetLevel(3, CCToNorm(cc_value));
            break;
        case TARGET_DRUM_5_LEVEL:
            sampler.SetLevel(4, CCToNorm(cc_value));
            break;
        case TARGET_DRUM_6_LEVEL:
            sampler.SetLevel(5, CCToNorm(cc_value));
            break;
        case TARGET_DRUM_7_LEVEL:
            sampler.SetLevel(6, CCToNorm(cc_value));
            break;
        case TARGET_DRUM_8_LEVEL:
            sampler.SetLevel(7, CCToNorm(cc_value));
            break;

        // Drum pans
        case TARGET_DRUM_1_PAN:
            sampler.SetPan(0, CCToPan(cc_value));
            break;
        case TARGET_DRUM_2_PAN:
            sampler.SetPan(1, CCToPan(cc_value));
            break;
        case TARGET_DRUM_3_PAN:
            sampler.SetPan(2, CCToPan(cc_value));
            break;
        case TARGET_DRUM_4_PAN:
            sampler.SetPan(3, CCToPan(cc_value));
            break;
        case TARGET_DRUM_5_PAN:
            sampler.SetPan(4, CCToPan(cc_value));
            break;
        case TARGET_DRUM_6_PAN:
            sampler.SetPan(5, CCToPan(cc_value));
            break;
        case TARGET_DRUM_7_PAN:
            sampler.SetPan(6, CCToPan(cc_value));
            break;
        case TARGET_DRUM_8_PAN:
            sampler.SetPan(7, CCToPan(cc_value));
            break;

        // Drum master level
        case TARGET_DRUM_MASTER_LEVEL:
            sampler.SetMasterLevel(CCToNorm(cc_value));
            break;

        // Global master output
        case TARGET_MASTER_OUTPUT:
            cc_engine.SetMasterOutput(CCToNorm(cc_value));
            break;

        default:
            break;
    }
}

/**
 * Automation playback callback - called when automation value changes
 * Applies CC value to synth (called from audio callback)
 */
void AutomationPlaybackCallback(uint8_t cc, uint8_t value)
{
    // Route through bank-aware CC engine (use synth bank for automation)
    // For now, apply to synth directly since automation is synth-focused
    uint8_t out_value;
    CCMap::ParamTarget target = cc_engine.ProcessCC(cc, value, out_value);
    if(target != CCMap::TARGET_NONE)
    {
        ApplyParamTarget(target, out_value);
    }
}

/**
 * Sequencer playback callback - called from audio callback for each event
 * Queues events for MIDI Monitor (main loop) and triggers sound immediately
 */
void SequencerPlaybackCallback(uint8_t status, uint8_t data1, uint8_t data2)
{
    // Queue for MIDI Monitor (processed in main loop)
    uint8_t next = (playback_queue_head + 1) % PLAYBACK_QUEUE_SIZE;
    if(next != playback_queue_tail)  // Not full
    {
        playback_queue[playback_queue_head].status = status;
        playback_queue[playback_queue_head].data1 = data1;
        playback_queue[playback_queue_head].data2 = data2;
        playback_queue_head = next;

        // Debug: track what's being queued
        uint8_t type = status & 0xF0;
        if(type == 0x90 && data2 > 0)
            debug_noteon_queued++;
        else if(type == 0x80 || (type == 0x90 && data2 == 0))
            debug_noteoff_queued++;
    }
    else
    {
        debug_queue_full++;
    }

    // Trigger sound immediately (real-time path)
    uint8_t channel = status & 0x0F;
    uint8_t type = status & 0xF0;

    if(channel == Sequencer::DRUM_CHANNEL)
    {
        // Drum note - trigger sampler
        if(type == 0x90 && data2 > 0)
        {
            sampler.TriggerMidi(channel, data1, data2);
        }
    }
    else if(channel == Sequencer::SYNTH_CHANNEL)
    {
        // Synth note - trigger synth
        if(type == 0x90 && data2 > 0)
        {
            synth.NoteOn(data1, data2);
        }
        else if(type == 0x80 || (type == 0x90 && data2 == 0))
        {
            synth.NoteOff(data1);
        }
    }
}

// Audio callback - processes transport timing, synth, and drums
void AudioCallback(AudioHandle::InputBuffer  in,
                   AudioHandle::OutputBuffer out,
                   size_t                    size)
{
    cpu_meter.OnBlockStart();

    for(size_t i = 0; i < size; i++)
    {
        // Process transport timing (once per sample)
        bool new_tick = transport.Process();

        // Process sequencer and automation playback on each new tick
        if(new_tick && (transport.IsPlaying() || transport.IsRecording()))
        {
            uint32_t tick = transport.GetPosition().tick;
            sequencer.Process(tick);
            automation.Process(tick, AutomationPlaybackCallback);
        }

        // Process synth engine (stereo)
        float synth_left, synth_right;
        synth.ProcessStereo(&synth_left, &synth_right);

        // Process drum sampler (stereo)
        float drum_left, drum_right;
        sampler.ProcessStereo(&drum_left, &drum_right);

        // Mix synth + drums with audio input (passthrough)
        // Apply master output level from CC engine
        float master = cc_engine.GetMasterOutput();
        out[0][i] = in[0][i] + (synth_left + drum_left) * master;
        out[1][i] = in[1][i] + (synth_right + drum_right) * master;
    }

    // Check if transport state changed (flag for main loop)
    if(transport.CheckStateChanged())
    {
        send_transport_update = true;
    }

    // Check if voice counts changed
    uint8_t synth_count = synth.GetActiveCount();
    uint8_t drum_count = sampler.GetActiveCount();
    if(synth_count != last_synth_count || drum_count != last_drum_count)
    {
        last_synth_count = synth_count;
        last_drum_count = drum_count;
        send_voices_update = true;
    }

    cpu_meter.OnBlockEnd();
}

// Transmit buffer for building messages
static uint8_t tx_buffer[Protocol::MAX_MESSAGE];

// Receive buffer and state
static uint8_t          rx_buffer[256];
static volatile uint32_t rx_len    = 0;
static volatile bool     rx_ready  = false;
static volatile bool     flash_led = false;

// Protocol parser for incoming binary messages
static Protocol::Parser parser;

// USB receive callback
void UsbReceiveCallback(uint8_t* buf, uint32_t* len)
{
    if(*len > 0 && *len < sizeof(rx_buffer))
    {
        memcpy((void*)rx_buffer, buf, *len);
        rx_len    = *len;
        rx_ready  = true;
        flash_led = true;
    }
}

// Send raw bytes over USB
void UsbSendRaw(const uint8_t* data, size_t len)
{
    hw.seed.usb_handle.TransmitInternal((uint8_t*)data, len);
}

// Send a text string (for backwards compatibility)
void UsbSendText(const char* str)
{
    UsbSendRaw((const uint8_t*)str, strlen(str));
}

// Send a binary protocol message
void SendMessage(uint8_t type, const uint8_t* payload, uint16_t len)
{
    size_t msg_len = Protocol::BuildMessage(tx_buffer, type, payload, len);
    UsbSendRaw(tx_buffer, msg_len);
}

// Send a TICK message with current position
void SendTick()
{
    const Transport::Position& pos = transport.GetPosition();
    uint8_t payload[4];
    payload[0] = pos.tick & 0xFF;
    payload[1] = (pos.tick >> 8) & 0xFF;
    payload[2] = (pos.tick >> 16) & 0xFF;
    payload[3] = (pos.tick >> 24) & 0xFF;
    SendMessage(Protocol::MSG_TICK, payload, 4);
}

// Send a TRANSPORT message with current state
void SendTransport()
{
    uint8_t playing   = transport.IsPlaying() || transport.IsRecording() ? 1 : 0;
    uint8_t recording = transport.IsRecording() ? 1 : 0;
    uint16_t bpm      = transport.GetBpm();

    uint8_t payload[4];
    payload[0] = playing;
    payload[1] = recording;
    payload[2] = bpm & 0xFF;
    payload[3] = (bpm >> 8) & 0xFF;
    SendMessage(Protocol::MSG_TRANSPORT, payload, 4);
}

// Send a DEBUG text message
void SendDebug(const char* text)
{
    size_t len = strlen(text);
    if(len > Protocol::MAX_PAYLOAD)
    {
        len = Protocol::MAX_PAYLOAD;
    }
    SendMessage(Protocol::MSG_DEBUG, (const uint8_t*)text, len);
}

// Send a MIDI_IN message (forward MIDI event to companion)
void SendMidiIn(uint8_t status, uint8_t data1, uint8_t data2)
{
    uint8_t payload[3] = {status, data1, data2};
    SendMessage(Protocol::MSG_MIDI_IN, payload, 3);
}

// Send a VOICES message with active voice counts
void SendVoices()
{
    uint8_t payload[2] = {last_synth_count, last_drum_count};
    SendMessage(Protocol::MSG_VOICES, payload, 2);
}

// Send current CC bank
void SendCCBank()
{
    uint8_t payload[1] = {static_cast<uint8_t>(cc_engine.GetBank())};
    SendMessage(Protocol::MSG_CC_BANK, payload, 1);
}

// Send fader pickup states
void SendFaderState()
{
    uint8_t payload[CCMap::NUM_FADERS];
    for(uint8_t i = 0; i < CCMap::NUM_FADERS; i++)
    {
        const CCMap::FaderState& state = cc_engine.GetFaderState(i);
        // Pack state: bit 0 = picked_up, bit 1 = needs_pickup
        payload[i] = (state.picked_up ? 0x01 : 0) | (state.needs_pickup ? 0x02 : 0);
    }
    SendMessage(Protocol::MSG_FADER_STATE, payload, CCMap::NUM_FADERS);
}

// Send mixer state (drum/synth levels, pans, masters)
void SendMixerState()
{
    // [drum_levels:8][drum_pans:8][drum_master:1][synth_level:1][synth_pan:1][synth_master:1][master_out:1]
    uint8_t payload[21];
    size_t idx = 0;

    // Drum levels (0-127)
    for(uint8_t i = 0; i < 8; i++)
    {
        payload[idx++] = CCMap::NormToCC(sampler.GetLevel(i));
    }

    // Drum pans (0=left, 64=center, 127=right)
    for(uint8_t i = 0; i < 8; i++)
    {
        payload[idx++] = CCMap::PanToCC(sampler.GetPan(i));
    }

    // Drum master level
    payload[idx++] = CCMap::NormToCC(sampler.GetMasterLevel());

    // Synth level, pan, master
    const Synth::SynthParams& p = synth.GetParams();
    payload[idx++] = CCMap::NormToCC(p.level);
    payload[idx++] = CCMap::PanToCC(p.pan);
    payload[idx++] = CCMap::NormToCC(p.master_level);

    // Master output
    payload[idx++] = CCMap::NormToCC(cc_engine.GetMasterOutput());

    SendMessage(Protocol::MSG_MIXER_STATE, payload, idx);
}

// Helper to write a float as 4 little-endian bytes
void WriteFloat(uint8_t* buf, float value)
{
    union { float f; uint8_t b[4]; } u;
    u.f = value;
    buf[0] = u.b[0];
    buf[1] = u.b[1];
    buf[2] = u.b[2];
    buf[3] = u.b[3];
}

// Helper to read a float from 4 little-endian bytes
float ReadFloat(const uint8_t* buf)
{
    union { float f; uint8_t b[4]; } u;
    u.b[0] = buf[0];
    u.b[1] = buf[1];
    u.b[2] = buf[2];
    u.b[3] = buf[3];
    return u.f;
}

// Send full synth state to companion
void SendSynthState()
{
    const Synth::SynthParams& p = synth.GetParams();

    // Payload: all params serialized as bytes/floats
    // Order must match companion's parsing
    // Size: 2 + 8 + 1 + 12 + 16 + 16 + 8 + 4 + 1 = 68 bytes
    uint8_t payload[72];
    size_t idx = 0;

    // Oscillators (2 bytes + 2 floats + 1 int8)
    payload[idx++] = p.osc1_wave;
    payload[idx++] = p.osc2_wave;
    WriteFloat(&payload[idx], p.osc1_level); idx += 4;
    WriteFloat(&payload[idx], p.osc2_level); idx += 4;
    payload[idx++] = static_cast<uint8_t>(p.osc2_detune + 24);  // Offset to unsigned

    // Filter (3 floats)
    WriteFloat(&payload[idx], p.filter_cutoff); idx += 4;
    WriteFloat(&payload[idx], p.filter_res); idx += 4;
    WriteFloat(&payload[idx], p.filter_env_amt); idx += 4;

    // Amp envelope (4 floats)
    WriteFloat(&payload[idx], p.amp_attack); idx += 4;
    WriteFloat(&payload[idx], p.amp_decay); idx += 4;
    WriteFloat(&payload[idx], p.amp_sustain); idx += 4;
    WriteFloat(&payload[idx], p.amp_release); idx += 4;

    // Filter envelope (4 floats)
    WriteFloat(&payload[idx], p.filt_attack); idx += 4;
    WriteFloat(&payload[idx], p.filt_decay); idx += 4;
    WriteFloat(&payload[idx], p.filt_sustain); idx += 4;
    WriteFloat(&payload[idx], p.filt_release); idx += 4;

    // Velocity sensitivity (2 floats)
    WriteFloat(&payload[idx], p.vel_to_amp); idx += 4;
    WriteFloat(&payload[idx], p.vel_to_filter); idx += 4;

    // Master level (1 float)
    WriteFloat(&payload[idx], p.level); idx += 4;

    // Current preset index
    payload[idx++] = synth.GetCurrentPreset();

    SendMessage(Protocol::MSG_SYNTH_STATE, payload, idx);
}

// Check if received text matches a command
bool MatchCommand(const char* cmd)
{
    size_t cmd_len = strlen(cmd);

    uint32_t check_len = rx_len;
    while(check_len > 0 &&
          (rx_buffer[check_len - 1] == '\r' ||
           rx_buffer[check_len - 1] == '\n'))
    {
        check_len--;
    }

    if(check_len != cmd_len)
        return false;

    for(size_t i = 0; i < cmd_len; i++)
    {
        char a = rx_buffer[i];
        char b = cmd[i];
        if(a >= 'a' && a <= 'z') a -= 32;
        if(b >= 'a' && b <= 'z') b -= 32;
        if(a != b)
            return false;
    }
    return true;
}

// Process incoming binary protocol commands
void ProcessBinaryCommand()
{
    switch(parser.type)
    {
        case Protocol::CMD_PLAY:
            // Capture base values for blend mode before starting
            automation.CaptureBaseValues();
            automation.ResetPlayback();
            transport.Play();
            SendTransport();
            SendDebug("CMD: PLAY");
            break;

        case Protocol::CMD_STOP:
            transport.Stop();
            synth.AllNotesOff();
            sequencer.ResetPlayback();
            automation.ResetPlayback();
            SendTransport();
            SendDebug("CMD: STOP");
            break;

        case Protocol::CMD_RECORD:
            transport.ToggleRecord();
            SendTransport();
            SendDebug("CMD: RECORD");
            break;

        case Protocol::CMD_TEMPO:
            if(parser.payload_len >= 2)
            {
                uint16_t bpm = parser.payload[0] | (parser.payload[1] << 8);
                transport.SetBpm(bpm);
                SendTransport();
                char buf[32];
                sprintf(buf, "CMD: TEMPO=%d", bpm);
                SendDebug(buf);
            }
            break;

        case Protocol::CMD_REQ_STATE:
            // Send current state
            SendTransport();
            SendTick();
            SendVoices();
            SendDebug("CMD: STATE");
            break;

        case Protocol::CMD_SYNTH_PARAM:
            if(parser.payload_len >= 5)
            {
                uint8_t param_id = parser.payload[0];
                float value = ReadFloat(&parser.payload[1]);
                if(param_id < Synth::PARAM_COUNT)
                {
                    synth.SetParam(static_cast<Synth::ParamId>(param_id), value);
                    SendSynthState();  // Confirm change
                }
            }
            break;

        case Protocol::CMD_LOAD_PRESET:
            if(parser.payload_len >= 1)
            {
                uint8_t preset = parser.payload[0];
                synth.LoadPreset(preset);
                SendSynthState();
                char buf[32];
                sprintf(buf, "Preset: %s", Synth::FactoryPresets::GetPresetName(preset));
                SendDebug(buf);
            }
            break;

        case Protocol::CMD_REQ_SYNTH:
            SendSynthState();
            SendDebug("CMD: SYNTH_STATE");
            break;

        case Protocol::CMD_SET_BANK:
            if(parser.payload_len >= 1)
            {
                uint8_t bank = parser.payload[0];
                if(bank < CCMap::NUM_BANKS)
                {
                    cc_engine.SetBank(static_cast<CCMap::Bank>(bank));
                    SendCCBank();
                    SendFaderState();
                    char buf[32];
                    sprintf(buf, "Bank: %s", cc_engine.GetBankName());
                    SendDebug(buf);
                }
            }
            break;

        default:
            SendDebug("CMD: Unknown");
            break;
    }
}

int main(void)
{
    // Initialize hardware
    hw.Init();

    // Enable FPU Flush-to-Zero mode to prevent denormalized float slowdowns
    // This is critical for audio DSP - denormals can cause 10-100x slowdown
    uint32_t fpscr = __get_FPSCR();
    fpscr |= (1 << 24);  // Set FZ (Flush-to-Zero) bit
    __set_FPSCR(fpscr);

    // Initialize USB CDC
    hw.seed.usb_handle.Init(UsbHandle::FS_INTERNAL);
    hw.seed.usb_handle.SetReceiveCallback(UsbReceiveCallback,
                                          UsbHandle::FS_INTERNAL);

    // Start ADC and Audio (required for full hardware init)
    hw.StartAdc();
    hw.SetAudioBlockSize(64);  // Larger block size for more CPU headroom with 6-voice synth
    hw.StartAudio(AudioCallback);

    // Initialize CPU load meter for diagnostics
    cpu_meter.Init(hw.AudioSampleRate(), hw.AudioBlockSize());

    // Initialize transport engine with audio sample rate
    transport.Init(hw.AudioSampleRate());

    // Initialize sequencer with pattern length from transport
    sequencer.Init(transport.GetPatternTicks());

    // Initialize automation with same pattern length
    automation.Init(transport.GetPatternTicks());

    // Initialize sampler and generate samples
    sampler.Init();
    sample_bank.Generate();

    // Initialize synth engine
    synth.Init(hw.AudioSampleRate());

    // Initialize MIDI router (connects sampler, synth, and companion)
    midi_router.Init(&sampler, &synth, SendMidiIn);
    midi_router.SetRecordCallback(RouterRecordCallback);

    // Initialize CC mapping engine (4-bank system)
    cc_engine.Init();

    // Connect sequencer playback to callback (for unified routing)
    sequencer.SetPlaybackCallback(SequencerPlaybackCallback);

    // Load samples into sampler slots (pads 0-7 = notes 36-43)
    sampler.LoadSample(0, sample_bank.kick, DrumSamples::KICK_LENGTH, "Kick");
    sampler.LoadSample(1, sample_bank.snare, DrumSamples::SNARE_LENGTH, "Snare");
    sampler.LoadSample(2, sample_bank.hihat_closed, DrumSamples::HIHAT_C_LENGTH, "HH Closed");
    sampler.LoadSample(3, sample_bank.hihat_open, DrumSamples::HIHAT_O_LENGTH, "HH Open");
    sampler.LoadSample(4, sample_bank.clap, DrumSamples::CLAP_LENGTH, "Clap");
    sampler.LoadSample(5, sample_bank.tom_low, DrumSamples::TOM_LOW_LENGTH, "Tom Low");
    sampler.LoadSample(6, sample_bank.tom_mid, DrumSamples::TOM_MID_LENGTH, "Tom Mid");
    sampler.LoadSample(7, sample_bank.rim, DrumSamples::RIM_LENGTH, "Rim");

    // Initialize MIDI input (UART on D14)
    hw.midi.StartReceive();

    // Initialize parser
    parser.Reset();

    // Give USB time to enumerate
    System::Delay(500);

    // Send startup message (as DEBUG)
    SendDebug("GroovyDaisy v1.0 - Synth + Drums");

    // Send initial state
    SendTransport();
    SendSynthState();
    SendCCBank();
    SendFaderState();
    SendMixerState();

    // Timing
    uint32_t last_tick_send      = 0;
    uint32_t last_transport_send = 0;
    uint32_t flash_start         = 0;
    bool     midi_flash          = false;

    // Double-stop detection for reset
    uint32_t last_stop_time = 0;

    // Main loop
    while(1)
    {
        uint32_t now = System::GetNow();

        // Process Pod controls (buttons, encoder)
        hw.ProcessAllControls();

        // Button 1: Play/Stop toggle
        if(hw.button1.RisingEdge())
        {
            if(transport.IsPlaying() || transport.IsRecording())
            {
                // First stop - just stop
                transport.Stop();
                synth.AllNotesOff();
                sequencer.ResetPlayback();
                automation.ResetPlayback();
                last_stop_time = now;
                SendDebug("Transport: Stop");
            }
            else
            {
                // Stopped: check for double-click to reset
                if(now - last_stop_time < 500)
                {
                    transport.StopAndReset();
                    sequencer.Clear();
                    sequencer.ResetPlayback();
                    automation.Clear();
                    automation.ResetPlayback();
                    SendDebug("Transport: Reset + Clear");
                }
                else
                {
                    // Starting playback - capture base values for blend mode
                    automation.CaptureBaseValues();
                    automation.ResetPlayback();
                    transport.Play();
                    SendDebug("Transport: Play");
                }
            }
            SendTransport();
        }

        // Button 2: Record toggle
        if(hw.button2.RisingEdge())
        {
            transport.ToggleRecord();
            if(transport.IsRecording())
            {
                sequencer.StartRecordPass();  // Reset for replace mode
                SendDebug("Transport: Record ON");
            }
            else
            {
                SendDebug("Transport: Record OFF");
            }
            SendTransport();
        }

        // Encoder rotation: Adjust tempo
        int32_t enc_inc = hw.encoder.Increment();
        if(enc_inc != 0)
        {
            transport.AdjustBpm(enc_inc);
            SendTransport();
            char buf[32];
            sprintf(buf, "BPM: %d", transport.GetBpm());
            SendDebug(buf);
        }

        // Encoder click: Toggle overdub/replace mode
        if(hw.encoder.RisingEdge())
        {
            sequencer.SetOverdubMode(!sequencer.IsOverdubMode());
            SendDebug(sequencer.IsOverdubMode() ? "Mode: Overdub" : "Mode: Replace");
        }

        // Send TICK message at ~5fps (every 200ms) - reduced to free USB bandwidth
        if(now - last_tick_send >= 200)
        {
            last_tick_send = now;
            SendTick();
        }


        // Send TRANSPORT message on state change from audio callback
        if(send_transport_update)
        {
            send_transport_update = false;
            SendTransport();
        }

        // Send VOICES message on count change
        if(send_voices_update)
        {
            send_voices_update = false;
            SendVoices();
        }

        // Periodic synth diagnostics (every 2 seconds)
        static uint32_t last_diag_send = 0;
        if(now - last_diag_send >= 2000)
        {
            last_diag_send = now;

            // Send voice state dump
            char diag[128];
            synth.GetVoiceDiagString(diag, sizeof(diag));
            SendDebug(diag);

            // Send CPU load (>80% = concern, >95% = audio dropouts)
            char cpu_buf[32];
            sprintf(cpu_buf, "CPU: %d%%", (int)(cpu_meter.GetAvgCpuLoad() * 100.0f));
            SendDebug(cpu_buf);

            // Send automation point count (if any recorded)
            uint16_t auto_points = automation.GetTotalPointCount();
            if(auto_points > 0)
            {
                char auto_buf[48];
                sprintf(auto_buf, "Auto: %d pts, Blend: %s",
                        auto_points,
                        automation.IsBlendEnabled() ? "ON" : "OFF");
                SendDebug(auto_buf);
            }

            // Report anomalies
            if(synth.HadNaN())
            {
                SendDebug("WARN: NaN detected, voice reset");
            }
            if(synth.HadStuckVoice())
            {
                SendDebug("WARN: Stuck voice killed after 3s");
            }
        }

        // Also send TRANSPORT periodically (every 500ms) for sync
        if(now - last_transport_send >= 500)
        {
            last_transport_send = now;
            SendTransport();
        }

        // Note: UI now updates from CC events via MSG_MIDI_IN
        // No need for periodic SendMixerState/SendSynthState

        // Process MIDI input
        hw.midi.Listen();

        while(hw.midi.HasEvents())
        {
            MidiEvent e = hw.midi.PopEvent();

            // Build status byte from type and channel
            uint8_t status = 0;
            uint8_t data1  = 0;
            uint8_t data2  = 0;

            switch(e.type)
            {
                case NoteOn:
                {
                    NoteOnEvent n = e.AsNoteOn();
                    bool recording = transport.IsRecording();
                    uint32_t tick = transport.GetPosition().tick;

                    // Per MIDI spec: NoteOn with velocity=0 is NoteOff
                    if(n.velocity == 0)
                    {
                        status = 0x80 | (e.channel & 0x0F);  // NoteOff

                        // Route through router (handles synth NoteOff)
                        midi_router.RouteNoteOff(e.channel, n.note,
                                                 MidiRouter::Source::LIVE_INPUT,
                                                 recording, tick);
                    }
                    else
                    {
                        status     = 0x90 | (e.channel & 0x0F);  // NoteOn
                        midi_flash = true;  // Flash LED on note on

                        // Route through router (handles sampler, synth, recording)
                        midi_router.RouteNoteOn(e.channel, n.note, n.velocity,
                                                MidiRouter::Source::LIVE_INPUT,
                                                recording, tick);
                    }
                    data1 = n.note;
                    data2 = n.velocity;
                    break;
                }
                case NoteOff:
                {
                    NoteOffEvent n = e.AsNoteOff();
                    status         = 0x80 | (e.channel & 0x0F);
                    data1          = n.note;
                    data2          = n.velocity;

                    // Route through router (handles synth NoteOff + recording)
                    midi_router.RouteNoteOff(e.channel, n.note,
                                             MidiRouter::Source::LIVE_INPUT,
                                             transport.IsRecording(),
                                             transport.GetPosition().tick);
                    break;
                }
                case ControlChange:
                {
                    ControlChangeEvent cc = e.AsControlChange();
                    status                = 0xB0 | (e.channel & 0x0F);
                    data1                 = cc.control_number;
                    data2                 = cc.value;

                    // Route through bank-aware CC engine
                    uint8_t out_value;
                    CCMap::ParamTarget target = cc_engine.ProcessCC(cc.control_number, cc.value, out_value);

                    if(target != CCMap::TARGET_NONE)
                    {
                        // Apply the routed parameter
                        ApplyParamTarget(target, out_value);
                    }

                    // Check for bank change immediately (more reliable than flag-based check)
                    if(cc_engine.BankChanged())
                    {
                        SendCCBank();
                        SendFaderState();
                        SendDebug("Bank changed via CC");
                    }

                    // Forward CC to companion for MIDI monitor
                    SendMidiIn(status, data1, data2);

                    // Automation: record CC if automated and in record mode
                    if(e.channel == Synth::SYNTH_CHANNEL &&
                       automation.IsAutomatedCC(cc.control_number))
                    {
                        // Always update current value for blend tracking
                        automation.UpdateCurrentValue(cc.control_number, cc.value);

                        // Record if in record mode
                        if(transport.IsRecording())
                        {
                            automation.RecordCC(transport.GetPosition().tick,
                                                cc.control_number, cc.value);
                        }
                    }
                    break;
                }
                case PitchBend:
                {
                    // PitchBend is 14-bit, send as two 7-bit values
                    status = 0xE0 | (e.channel & 0x0F);
                    data1  = e.data[0] & 0x7F;  // LSB
                    data2  = e.data[1] & 0x7F;  // MSB
                    break;
                }
                case ChannelPressure:
                {
                    status = 0xD0 | (e.channel & 0x0F);
                    data1  = e.data[0];
                    data2  = 0;
                    break;
                }
                case PolyphonicKeyPressure:
                {
                    status = 0xA0 | (e.channel & 0x0F);
                    data1  = e.data[0];  // Note
                    data2  = e.data[1];  // Pressure
                    break;
                }
                default:
                    // Skip system messages for now
                    continue;
            }

            // Forward non-routed events to companion
            // (NoteOn/NoteOff/CC are already sent by the router)
            if(e.type != NoteOn && e.type != NoteOff && e.type != ControlChange)
            {
                SendMidiIn(status, data1, data2);
            }
        }

        // Process playback queue (sequencer events -> MIDI Monitor)
        while(playback_queue_tail != playback_queue_head)
        {
            // Copy values BEFORE incrementing tail to avoid race
            uint8_t status = playback_queue[playback_queue_tail].status;
            uint8_t data1 = playback_queue[playback_queue_tail].data1;
            uint8_t data2 = playback_queue[playback_queue_tail].data2;
            playback_queue_tail = (playback_queue_tail + 1) % PLAYBACK_QUEUE_SIZE;
            SendMidiIn(status, data1, data2);
        }

        // Process received USB data
        if(rx_ready)
        {
            // Null-terminate for text commands
            rx_buffer[rx_len] = '\0';

            // First check if it's a text command (for testing)
            if(MatchCommand("PING"))
            {
                UsbSendText("PONG\r\n");
            }
            else if(MatchCommand("STATUS"))
            {
                const Transport::Position& pos = transport.GetPosition();
                char buf[96];
                sprintf(buf, "STATUS: %s BPM=%d Bar=%d Beat=%d Tick=%lu\r\n",
                        transport.IsRecording() ? "REC" :
                        transport.IsPlaying() ? "PLAY" : "STOP",
                        transport.GetBpm(),
                        pos.bar,
                        pos.beat,
                        pos.tick);
                UsbSendText(buf);
            }
            else
            {
                // Try to parse as binary protocol
                for(uint32_t i = 0; i < rx_len; i++)
                {
                    if(parser.Feed(rx_buffer[i]))
                    {
                        ProcessBinaryCommand();
                    }
                }
            }

            rx_ready = false;
        }

        // LED2 flash on USB receive (cyan) or MIDI note (magenta)
        if(midi_flash)
        {
            hw.led2.Set(1.0f, 0.0f, 1.0f);  // Magenta for MIDI
            flash_start = now;
            midi_flash  = false;
        }
        else if(flash_led)
        {
            hw.led2.Set(0.0f, 1.0f, 1.0f);  // Cyan for USB
            flash_start = now;
            flash_led   = false;
        }
        else if(now - flash_start > 100)
        {
            hw.led2.Set(0.0f, 0.0f, 0.0f);
        }

        // LED1 shows transport state with beat pulse
        {
            const Transport::Position& pos = transport.GetPosition();
            bool on_beat = (pos.pulse < 12);  // Flash for first ~12 ticks of beat

            if(transport.IsRecording())
            {
                // Red for recording, pulse on beat
                hw.led1.Set(on_beat ? 1.0f : 0.3f, 0.0f, 0.0f);
            }
            else if(transport.IsPlaying())
            {
                // Green for playing, pulse on beat
                hw.led1.Set(0.0f, on_beat ? 1.0f : 0.3f, 0.0f);
            }
            else
            {
                // Dim blue for stopped
                hw.led1.Set(0.0f, 0.0f, 0.15f);
            }
        }

        hw.UpdateLeds();
        System::Delay(1);
    }
}
