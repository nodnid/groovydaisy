/**
 * GroovyDaisy v2 — campfire jam box (SPEC.md is the authoritative design)
 *
 * Phase 1: live instrument core. Keys -> 6-voice synth, pads -> 8-voice
 * sample drums, guitar -> monitored pass-through, all through the mixer
 * with a metronome, on the v2 event-driven protocol.
 *
 * main.cpp is WIRING ONLY: hardware init, the audio callback, the main
 * loop, and protocol glue. All logic lives in the src/ headers, which are
 * host-compilable except synth.h/sampler-DSP (DaisySP) — see CLAUDE.md.
 *
 * Real-time rules (SPEC.md firmware architecture):
 * - audio callback: no allocation, no printf, no USB
 * - callback <-> main loop only via SpscRing / atomics
 * - one MIDI path: live + (Phase 2) sequenced events through midi_router
 */

#include <stdio.h>
#include <string.h>
#include <atomic>
#include "daisy_pod.h"
#include "daisysp.h"
#include "protocol.h"
#include "clock.h"
#include "rt_queue.h"
#include "mixer.h"
#include "metronome.h"
#include "sampler.h"
#include "synth.h"
#include "cc_map.h"
#include "midi_router.h"
#include "ui_link.h"
#include "samples/drums.h"
#include "util/CpuLoadMeter.h"

using namespace daisy;

// ---------------------------------------------------------------------------
// Globals (wiring only)
// ---------------------------------------------------------------------------

DaisyPod           hw;
Clock::Engine      clk;
Mixer::Engine      mixer;
Metronome::Engine  metro;
Sampler::Engine    sampler;
Synth::Engine      synth;
MidiRouter::Router router;
CCMap::Engine      cc_engine;
UiLink::Publisher  ui;
CpuLoadMeter       cpu_meter;

// ---------------------------------------------------------------------------
// SDRAM regions — the ENTIRE 64 MB budget is declared here (SPEC.md memory
// budget), even for modules that arrive in later phases, so the layout risk
// is retired now. libDaisy does NOT zero SDRAM BSS: every region is
// explicitly initialized in main() before audio starts.
// ---------------------------------------------------------------------------

// Drum sample bank (synthesized at boot; ~400 KB today, region grows later)
DrumSamples::SampleBank DSY_SDRAM_BSS sample_bank;

// Audio capture ring: 8 bars @ 60 BPM 4/4 = 32 s mono s16 (Phase 3)
constexpr size_t CAPTURE_RING_SAMPLES = 32UL * 48000;
int16_t DSY_SDRAM_BSS capture_ring_mem[CAPTURE_RING_SAMPLES];

// Granule pool for audio loop tracks: 42 MB of mono s16 (Phase 3)
constexpr size_t GRANULE_POOL_SAMPLES = 42UL * 1024 * 1024 / sizeof(int16_t);
int16_t DSY_SDRAM_BSS granule_pool_mem[GRANULE_POOL_SAMPLES];

// FX delay/reverb lines (Phase 5)
uint8_t DSY_SDRAM_BSS fx_mem[2UL * 1024 * 1024];

static_assert(sizeof(sample_bank) + sizeof(capture_ring_mem)
                      + sizeof(granule_pool_mem) + sizeof(fx_mem)
                  <= 62UL * 1024 * 1024,
              "SDRAM budget exceeded (64 MB minus headroom)");

// ---------------------------------------------------------------------------
// Callback <-> main loop channels
// ---------------------------------------------------------------------------

// Live MIDI notes: main loop (UART parse) -> audio callback (dispatch)
SpscRing<MidiRouter::Event, 128> midi_to_audio;

// Voice activity: audio callback -> main loop (publish on change)
std::atomic<uint8_t> synth_voices_active{0};
std::atomic<uint8_t> drum_voices_active{0};

// ---------------------------------------------------------------------------
// Audio callback
// ---------------------------------------------------------------------------

void AudioCallback(AudioHandle::InputBuffer  in,
                   AudioHandle::OutputBuffer out,
                   size_t                    size)
{
    cpu_meter.OnBlockStart();

    Clock::TickBlock tb;
    clk.Advance(size, tb);

    // Drain live MIDI (block-start dispatch: <=1 block of jitter on live
    // input is inaudible; sequenced playback gets sample-accurate dispatch
    // via tick frame offsets from Phase 2)
    MidiRouter::Event ev;
    while(midi_to_audio.Pop(ev))
    {
        router.Dispatch(ev, MidiRouter::Source::Live, clk.NowTick());
    }

    size_t tick_i = 0;
    for(size_t i = 0; i < size; i++)
    {
        // Fire tick-aligned events at their exact frame
        while(tick_i < tb.count && tb.frame[tick_i] == i)
        {
            uint32_t t = tb.tick[tick_i];
            if(Clock::Engine::OnBeat(t))
            {
                metro.TriggerBeat(Clock::Engine::OnBar(t));
            }
            tick_i++;
        }

        float sl, sr, dl, dr;
        synth.ProcessStereo(&sl, &sr);
        sampler.ProcessStereo(&dl, &dr);

        float l = 0.0f, r = 0.0f;
        mixer.AddStereo(Mixer::STRIP_SYNTH, sl, sr, l, r);
        mixer.AddStereo(Mixer::STRIP_DRUMS, dl, dr, l, r);
        mixer.AddMono(Mixer::STRIP_METRO, metro.Process(), l, r);
        mixer.AddMono(Mixer::STRIP_GUITAR, in[0][i], l, r);
        mixer.ApplyMaster(l, r);

        out[0][i] = l;
        out[1][i] = r;
    }

    synth_voices_active.store(synth.GetActiveCount(),
                              std::memory_order_relaxed);
    drum_voices_active.store(sampler.GetActiveCount(),
                             std::memory_order_relaxed);

    cpu_meter.OnBlockEnd();
}

// ---------------------------------------------------------------------------
// USB CDC plumbing
// ---------------------------------------------------------------------------

static uint8_t           rx_buffer[256];
static volatile uint32_t rx_len   = 0;
static volatile bool     rx_ready = false;

static Protocol::Parser parser;

void UsbReceiveCallback(uint8_t* buf, uint32_t* len)
{
    if(*len > 0 && *len < sizeof(rx_buffer))
    {
        memcpy((void*)rx_buffer, buf, *len);
        rx_len   = *len;
        rx_ready = true;
    }
}

void UsbSendRaw(const uint8_t* data, size_t len)
{
    hw.seed.usb_handle.TransmitInternal((uint8_t*)data, len);
}

// ---------------------------------------------------------------------------
// State publishing helpers (payloads that depend on engine headers live
// here; simple ones are typed methods on UiLink::Publisher)
// ---------------------------------------------------------------------------

static void WriteFloatLE(uint8_t* buf, float value)
{
    union { float f; uint8_t b[4]; } u;
    u.f    = value;
    buf[0] = u.b[0];
    buf[1] = u.b[1];
    buf[2] = u.b[2];
    buf[3] = u.b[3];
}

static float ReadFloatLE(const uint8_t* buf)
{
    union { float f; uint8_t b[4]; } u;
    u.b[0] = buf[0];
    u.b[1] = buf[1];
    u.b[2] = buf[2];
    u.b[3] = buf[3];
    return u.f;
}

// Layout unchanged from v1 (companion protocol.ts mirrors it)
void SendSynthState()
{
    const Synth::SynthParams& p = synth.GetParams();
    uint8_t payload[72];
    size_t  idx = 0;

    payload[idx++] = p.osc1_wave;
    payload[idx++] = p.osc2_wave;
    WriteFloatLE(&payload[idx], p.osc1_level); idx += 4;
    WriteFloatLE(&payload[idx], p.osc2_level); idx += 4;
    payload[idx++] = static_cast<uint8_t>(p.osc2_detune + 24);

    WriteFloatLE(&payload[idx], p.filter_cutoff); idx += 4;
    WriteFloatLE(&payload[idx], p.filter_res); idx += 4;
    WriteFloatLE(&payload[idx], p.filter_env_amt); idx += 4;

    WriteFloatLE(&payload[idx], p.amp_attack); idx += 4;
    WriteFloatLE(&payload[idx], p.amp_decay); idx += 4;
    WriteFloatLE(&payload[idx], p.amp_sustain); idx += 4;
    WriteFloatLE(&payload[idx], p.amp_release); idx += 4;

    WriteFloatLE(&payload[idx], p.filt_attack); idx += 4;
    WriteFloatLE(&payload[idx], p.filt_decay); idx += 4;
    WriteFloatLE(&payload[idx], p.filt_sustain); idx += 4;
    WriteFloatLE(&payload[idx], p.filt_release); idx += 4;

    WriteFloatLE(&payload[idx], p.vel_to_amp); idx += 4;
    WriteFloatLE(&payload[idx], p.vel_to_filter); idx += 4;

    WriteFloatLE(&payload[idx], p.level); idx += 4;

    payload[idx++] = synth.GetCurrentPreset();

    ui.Send(Protocol::MSG_SYNTH_STATE, payload, idx);
}

void SendMixerStrip(uint8_t strip)
{
    const Mixer::Strip& s = mixer.Get(strip);
    ui.MixerStrip(strip, Mixer::GainToCc(s.gain), Mixer::PanToCc(s.pan),
                  s.mute, Mixer::GainToCc(s.send_rev),
                  Mixer::GainToCc(s.send_dly));
}

void SendTransport()
{
    ui.Transport(clk.Playing(), clk.Locked(), clk.Bpm());
}

void SendFaderState()
{
    uint8_t flags[CCMap::NUM_FADERS];
    for(uint8_t i = 0; i < CCMap::NUM_FADERS; i++)
    {
        const CCMap::FaderState& s = cc_engine.GetFaderState(i);
        flags[i] = (s.picked_up ? 0x01 : 0) | (s.needs_pickup ? 0x02 : 0);
    }
    ui.FaderState(flags, CCMap::NUM_FADERS);
}

void SendEngineMix();

/** Full snapshot: everything the companion needs to cold-join. */
void SendSnapshot()
{
    ui.Hello();
    SendTransport();
    ui.Sync(clk.NowTick());
    ui.Voices(synth_voices_active.load(std::memory_order_relaxed),
              drum_voices_active.load(std::memory_order_relaxed));
    ui.Bank(static_cast<uint8_t>(cc_engine.GetBank()));
    SendFaderState();
    SendMixerStrip(Mixer::STRIP_GUITAR);
    SendMixerStrip(Mixer::STRIP_SYNTH);
    SendMixerStrip(Mixer::STRIP_DRUMS);
    SendMixerStrip(Mixer::STRIP_METRO);
    ui.Metro(metro.Enabled(),
             Mixer::GainToCc(mixer.Get(Mixer::STRIP_METRO).gain));
    SendEngineMix();
    SendSynthState();
}

// Layout identical to v1 MSG_MIXER_STATE (companion parsing unchanged):
// [drum_levels:8][drum_pans:8][drum_master][synth_level][synth_pan]
// [synth_master][master_out], all CC scale
void SendEngineMix()
{
    uint8_t payload[21];
    size_t  idx = 0;
    for(uint8_t i = 0; i < 8; i++)
        payload[idx++] = CCMap::NormToCC(sampler.GetLevel(i));
    for(uint8_t i = 0; i < 8; i++)
        payload[idx++] = CCMap::PanToCC(sampler.GetPan(i));
    payload[idx++] = CCMap::NormToCC(sampler.GetMasterLevel());

    const Synth::SynthParams& p = synth.GetParams();
    payload[idx++]              = CCMap::NormToCC(p.level);
    payload[idx++]              = CCMap::PanToCC(p.pan);
    payload[idx++]              = CCMap::NormToCC(p.master_level);
    payload[idx++]              = CCMap::NormToCC(mixer.Master());

    ui.Send(Protocol::MSG_ENGINE_MIX, payload, idx);
}

// Chatty dumps are throttled: knob sweeps mark them dirty and the main
// loop flushes at most every 100 ms (don't spam CDC)
static bool synth_state_dirty = false;
static bool engine_mix_dirty  = false;

// ---------------------------------------------------------------------------
// CC -> parameter application
// ---------------------------------------------------------------------------

void ApplyParamTarget(CCMap::ParamTarget target, uint8_t cc_value)
{
    using namespace CCMap;

    switch(target)
    {
        // --- synth ---
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

        // --- drums (per-voice, inside the sampler engine) ---
        case TARGET_DRUM_1_LEVEL:
        case TARGET_DRUM_2_LEVEL:
        case TARGET_DRUM_3_LEVEL:
        case TARGET_DRUM_4_LEVEL:
        case TARGET_DRUM_5_LEVEL:
        case TARGET_DRUM_6_LEVEL:
        case TARGET_DRUM_7_LEVEL:
        case TARGET_DRUM_8_LEVEL:
            sampler.SetLevel(target - TARGET_DRUM_1_LEVEL, CCToNorm(cc_value));
            break;
        case TARGET_DRUM_1_PAN:
        case TARGET_DRUM_2_PAN:
        case TARGET_DRUM_3_PAN:
        case TARGET_DRUM_4_PAN:
        case TARGET_DRUM_5_PAN:
        case TARGET_DRUM_6_PAN:
        case TARGET_DRUM_7_PAN:
        case TARGET_DRUM_8_PAN:
            sampler.SetPan(target - TARGET_DRUM_1_PAN, CCToPan(cc_value));
            break;
        case TARGET_DRUM_MASTER_LEVEL:
            sampler.SetMasterLevel(CCToNorm(cc_value));
            break;

        // --- v2 mixer strips ---
        case TARGET_GUITAR_LEVEL:
            mixer.Get(Mixer::STRIP_GUITAR).gain = CCToNorm(cc_value);
            SendMixerStrip(Mixer::STRIP_GUITAR);
            break;
        case TARGET_GUITAR_PAN:
            mixer.Get(Mixer::STRIP_GUITAR).pan = CCToPan(cc_value);
            SendMixerStrip(Mixer::STRIP_GUITAR);
            break;
        case TARGET_METRO_LEVEL:
            mixer.Get(Mixer::STRIP_METRO).gain = CCToNorm(cc_value);
            ui.Metro(metro.Enabled(), cc_value);
            break;
        case TARGET_MASTER_OUTPUT:
            mixer.SetMaster(CCToNorm(cc_value));
            break;

        default:
            break;
    }

    // Synth-targeted CCs mark the (throttled) synth state dump dirty
    if(target >= CCMap::TARGET_SYNTH_OSC1_WAVE
       && target <= CCMap::TARGET_SYNTH_MASTER_LEVEL)
    {
        synth_state_dirty = true;
    }
    // Engine-mix targets (drum voice levels/pans, synth level/pan/master,
    // master out) mark the engine-mix dump dirty
    if((target >= CCMap::TARGET_DRUM_1_LEVEL
        && target <= CCMap::TARGET_MASTER_OUTPUT)
       || target == CCMap::TARGET_SYNTH_LEVEL
       || target == CCMap::TARGET_SYNTH_PAN
       || target == CCMap::TARGET_SYNTH_MASTER_LEVEL)
    {
        engine_mix_dirty = true;
    }
}

// ---------------------------------------------------------------------------
// Companion command handling
// ---------------------------------------------------------------------------

void ProcessCommand()
{
    switch(parser.type)
    {
        case Protocol::CMD_PLAY:
            clk.Play();
            SendTransport();
            break;

        case Protocol::CMD_STOP:
            clk.Stop();
            synth.AllNotesOff();
            SendTransport();
            break;

        case Protocol::CMD_REWIND:
            clk.Rewind();
            synth.AllNotesOff();
            SendTransport();
            ui.Sync(clk.NowTick());
            break;

        case Protocol::CMD_TEMPO:
            if(parser.payload_len >= 2)
            {
                uint16_t bpm_x10 = parser.payload[0]
                                   | (parser.payload[1] << 8);
                if(!clk.SetBpm((float)bpm_x10 / 10.0f))
                {
                    ui.Error(Protocol::ERR_TEMPO_LOCKED, 0);
                }
                SendTransport();
            }
            break;

        case Protocol::CMD_TAP:
            if(!clk.Tap(daisy::System::GetNow()))
            {
                ui.Error(Protocol::ERR_TEMPO_LOCKED, 0);
            }
            SendTransport();
            break;

        case Protocol::CMD_SYNTH_PARAM:
            if(parser.payload_len >= 5)
            {
                uint8_t param_id = parser.payload[0];
                float   value    = ReadFloatLE(&parser.payload[1]);
                if(param_id < Synth::PARAM_COUNT)
                {
                    synth.SetParam(static_cast<Synth::ParamId>(param_id),
                                   value);
                    SendSynthState();
                }
            }
            break;

        case Protocol::CMD_LOAD_PRESET:
            if(parser.payload_len >= 1)
            {
                synth.LoadPreset(parser.payload[0]);
                SendSynthState();
            }
            break;

        case Protocol::CMD_SET_BANK:
            if(parser.payload_len >= 1
               && parser.payload[0] < CCMap::NUM_BANKS)
            {
                cc_engine.SetBank(
                    static_cast<CCMap::Bank>(parser.payload[0]));
                ui.Bank(parser.payload[0]);
                SendFaderState();
            }
            break;

        case Protocol::CMD_MIXER:
            if(parser.payload_len >= 3)
            {
                uint8_t strip = parser.payload[0];
                uint8_t field = parser.payload[1];
                uint8_t value = parser.payload[2];
                if(strip == Protocol::MIX_STRIP_MASTER)
                {
                    if(field == Protocol::MIX_FIELD_LEVEL)
                    {
                        mixer.SetMaster(Mixer::CcToGain(value));
                        SendEngineMix();
                    }
                }
                else if(strip < Mixer::NUM_STRIPS)
                {
                    Mixer::Strip& s = mixer.Get(strip);
                    switch(field)
                    {
                        case Protocol::MIX_FIELD_LEVEL:
                            s.gain = Mixer::CcToGain(value);
                            break;
                        case Protocol::MIX_FIELD_PAN:
                            s.pan = Mixer::CcToPan(value);
                            break;
                        case Protocol::MIX_FIELD_MUTE:
                            s.mute = value != 0;
                            break;
                        case Protocol::MIX_FIELD_SEND_REV:
                            s.send_rev = Mixer::CcToGain(value);
                            break;
                        case Protocol::MIX_FIELD_SEND_DLY:
                            s.send_dly = Mixer::CcToGain(value);
                            break;
                        default: break;
                    }
                    SendMixerStrip(strip);
                }
            }
            break;

        case Protocol::CMD_METRO:
            if(parser.payload_len >= 2)
            {
                metro.SetEnabled(parser.payload[0] != 0);
                mixer.Get(Mixer::STRIP_METRO).gain
                    = Mixer::CcToGain(parser.payload[1]);
                ui.Metro(metro.Enabled(), parser.payload[1]);
            }
            break;

        case Protocol::CMD_MONITOR:
            if(parser.payload_len >= 1)
            {
                ui.SetMonitor(parser.payload[0] != 0);
            }
            break;

        case Protocol::CMD_REQ_STATE:
            SendSnapshot();
            break;

        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(void)
{
    hw.Init();

    // FPU flush-to-zero: denormals cause 10-100x DSP slowdowns
    uint32_t fpscr = __get_FPSCR();
    fpscr |= (1 << 24);
    __set_FPSCR(fpscr);

    // USB CDC
    hw.seed.usb_handle.Init(UsbHandle::FS_INTERNAL);
    hw.seed.usb_handle.SetReceiveCallback(UsbReceiveCallback,
                                          UsbHandle::FS_INTERNAL);

    // SDRAM regions: libDaisy does NOT zero these. The sample bank is
    // fully written by Generate(); ring/pool/fx are zeroed on first use by
    // their phases — but zero the ring now so Phase 3 testing can't read
    // garbage from a stale region.
    // (memset of 42 MB pool deferred to pool Lock() — takes ~100 ms and
    // granules are fully written on capture anyway.)
    memset(capture_ring_mem, 0, sizeof(capture_ring_mem));

    // Engines
    hw.SetAudioBlockSize(64);
    clk.Init(hw.AudioSampleRate());
    mixer.Init();
    metro.Init(hw.AudioSampleRate());
    metro.SetEnabled(true); // the grid must be audible before drums exist
    sampler.Init();
    sample_bank.Generate();
    synth.Init(hw.AudioSampleRate());
    router.Init(&sampler, &synth);
    cc_engine.Init();
    ui.Init(UsbSendRaw);
    cpu_meter.Init(hw.AudioSampleRate(), hw.AudioBlockSize());

    sampler.LoadSample(0, sample_bank.kick, DrumSamples::KICK_LENGTH, "Kick");
    sampler.LoadSample(1, sample_bank.snare, DrumSamples::SNARE_LENGTH, "Snare");
    sampler.LoadSample(2, sample_bank.hihat_closed, DrumSamples::HIHAT_C_LENGTH, "HH Closed");
    sampler.LoadSample(3, sample_bank.hihat_open, DrumSamples::HIHAT_O_LENGTH, "HH Open");
    sampler.LoadSample(4, sample_bank.clap, DrumSamples::CLAP_LENGTH, "Clap");
    sampler.LoadSample(5, sample_bank.tom_low, DrumSamples::TOM_LOW_LENGTH, "Tom Low");
    sampler.LoadSample(6, sample_bank.tom_mid, DrumSamples::TOM_MID_LENGTH, "Tom Mid");
    sampler.LoadSample(7, sample_bank.rim, DrumSamples::RIM_LENGTH, "Rim");

    hw.StartAdc();
    hw.StartAudio(AudioCallback);
    hw.midi.StartReceive();
    parser.Reset();

    System::Delay(500); // USB enumeration
    SendSnapshot();
    ui.Debug("GroovyDaisy v2 phase 1");

    uint32_t last_sync_send        = 0;
    uint32_t last_synth_state_send = 0;
    uint32_t last_engine_mix_send  = 0;
    uint32_t last_stop_time        = 0;
    uint8_t  last_synth_voices     = 0;
    uint8_t  last_drum_voices      = 0;
    bool     midi_flash            = false;
    uint32_t flash_start           = 0;

    while(1)
    {
        uint32_t now = System::GetNow();

        hw.ProcessAllControls();

        // Button 1: play/stop; second press within 500 ms of stopping =
        // rewind. NEVER clears content (SPEC.md: clearing is explicit).
        if(hw.button1.RisingEdge())
        {
            if(clk.Playing())
            {
                clk.Stop();
                synth.AllNotesOff();
                last_stop_time = now;
            }
            else if(now - last_stop_time < 500)
            {
                clk.Rewind();
                ui.Sync(clk.NowTick());
            }
            else
            {
                clk.Play();
            }
            SendTransport();
        }

        // Button 2: metronome toggle (Phase 2 reassigns this to undo)
        if(hw.button2.RisingEdge())
        {
            metro.SetEnabled(!metro.Enabled());
            ui.Metro(metro.Enabled(),
                     Mixer::GainToCc(mixer.Get(Mixer::STRIP_METRO).gain));
        }

        // Encoder: turn = tempo, press = tap
        int32_t enc = hw.encoder.Increment();
        if(enc != 0)
        {
            if(!clk.AdjustBpm((float)enc))
            {
                ui.Error(Protocol::ERR_TEMPO_LOCKED, 0);
            }
        }
        if(hw.encoder.RisingEdge())
        {
            if(!clk.Tap(now))
            {
                ui.Error(Protocol::ERR_TEMPO_LOCKED, 0);
            }
        }

        // Transport/tempo dirty -> publish
        if(clk.CheckDirty())
        {
            SendTransport();
        }

        // SYNC at ~5 Hz while playing (companion interpolates between)
        if(clk.Playing() && now - last_sync_send >= 200)
        {
            last_sync_send = now;
            ui.Sync(clk.NowTick());
        }

        // Voice activity on change
        {
            uint8_t s = synth_voices_active.load(std::memory_order_relaxed);
            uint8_t d = drum_voices_active.load(std::memory_order_relaxed);
            if(s != last_synth_voices || d != last_drum_voices)
            {
                last_synth_voices = s;
                last_drum_voices  = d;
                ui.Voices(s, d);
            }
        }

        // Throttled dumps after knob sweeps
        if(synth_state_dirty && now - last_synth_state_send >= 100)
        {
            synth_state_dirty     = false;
            last_synth_state_send = now;
            SendSynthState();
        }
        if(engine_mix_dirty && now - last_engine_mix_send >= 100)
        {
            engine_mix_dirty     = false;
            last_engine_mix_send = now;
            SendEngineMix();
        }

        // ------------------------------------------------------------------
        // UART MIDI from the KeyLab
        // ------------------------------------------------------------------
        hw.midi.Listen();
        while(hw.midi.HasEvents())
        {
            MidiEvent e = hw.midi.PopEvent();

            switch(e.type)
            {
                case NoteOn:
                case NoteOff:
                {
                    uint8_t note, vel;
                    if(e.type == NoteOn)
                    {
                        NoteOnEvent n = e.AsNoteOn();
                        note          = n.note;
                        vel           = n.velocity;
                    }
                    else
                    {
                        NoteOffEvent n = e.AsNoteOff();
                        note           = n.note;
                        vel            = 0;
                    }
                    bool is_off = (e.type == NoteOff) || (vel == 0);

                    MidiRouter::Event ev;
                    ev.status = (is_off ? 0x80 : 0x90) | (e.channel & 0x0F);
                    ev.d1     = note;
                    ev.d2     = vel;
                    midi_to_audio.Push(ev);

                    if(!is_off)
                        midi_flash = true;
                    if(ui.MonitorEnabled())
                        ui.MidiIn(ev.status, ev.d1, ev.d2);
                    break;
                }

                case ControlChange:
                {
                    ControlChangeEvent cc = e.AsControlChange();

                    uint8_t            out_value;
                    CCMap::ParamTarget target = cc_engine.ProcessCC(
                        cc.control_number, cc.value, out_value);
                    if(target != CCMap::TARGET_NONE)
                    {
                        ApplyParamTarget(target, out_value);
                    }
                    if(cc_engine.BankChanged())
                    {
                        ui.Bank(static_cast<uint8_t>(cc_engine.GetBank()));
                        SendFaderState();
                    }
                    if(ui.MonitorEnabled())
                    {
                        ui.MidiIn(0xB0 | (e.channel & 0x0F),
                                  cc.control_number, cc.value);
                    }
                    break;
                }

                default:
                    // other channel/system messages: monitor echo only
                    if(ui.MonitorEnabled() && e.type == PitchBend)
                    {
                        ui.MidiIn(0xE0 | (e.channel & 0x0F),
                                  e.data[0] & 0x7F, e.data[1] & 0x7F);
                    }
                    break;
            }
        }

        // ------------------------------------------------------------------
        // USB protocol input
        // ------------------------------------------------------------------
        if(rx_ready)
        {
            for(uint32_t i = 0; i < rx_len; i++)
            {
                if(parser.Feed(rx_buffer[i]))
                {
                    ProcessCommand();
                }
            }
            rx_ready = false;
        }

        // ------------------------------------------------------------------
        // LEDs: LED1 transport (green pulse on beat / dim blue stopped),
        // LED2 flashes magenta on note input
        // ------------------------------------------------------------------
        if(midi_flash)
        {
            hw.led2.Set(1.0f, 0.0f, 1.0f);
            flash_start = now;
            midi_flash  = false;
        }
        else if(now - flash_start > 100)
        {
            hw.led2.Set(0.0f, 0.0f, 0.0f);
        }

        {
            uint32_t t       = clk.NowTick();
            bool     on_beat = (t % Clock::PPQN) < 12;
            if(clk.Playing())
            {
                hw.led1.Set(0.0f, on_beat ? 1.0f : 0.3f, 0.0f);
            }
            else
            {
                hw.led1.Set(0.0f, 0.0f, 0.15f);
            }
        }

        hw.UpdateLeds();
        System::Delay(1);
    }
}
