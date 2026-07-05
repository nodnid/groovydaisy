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
#include "track.h"
#include "capture.h"
#include "seq_track.h"
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

// Phase 2: track registry + retrospective MIDI capture
Track::Registry  registry;
Capture::MidiRing pads_ring;
Capture::MidiRing keys_ring;
Capture::Pending  pending_cap[2];          // indexed by SRC_PADS / SRC_KEYS
bool              pending_silent[2];        // SRC_ANY captures skip ERR_EMPTY
uint8_t           src_len_preset[3] = {4, 4, 4}; // bars per source

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
// Capture recording + sequenced playback glue (audio-callback context)
// ---------------------------------------------------------------------------

// Router record hook: every live note lands in its source's rolling ring.
// Only while the clock runs — events stamped with a frozen tick would
// pollute later capture windows.
void RecordToRings(const MidiRouter::Event& e, uint32_t tick)
{
    // Not while stopped (frozen ticks would pollute later windows) and not
    // during the count-in (the grid hasn't started; everything would pile
    // up on one tick)
    if(!clk.Playing() || clk.InPreroll())
        return;
    uint8_t ch = e.status & 0x0F;
    if(ch == MidiRouter::DRUM_CHANNEL)
        pads_ring.Push(tick, e.status, e.d1, e.d2);
    else if(ch == MidiRouter::SYNTH_CHANNEL)
        keys_ring.Push(tick, e.status, e.d1, e.d2);
}

// SeqTrack dispatch -> the same router path as live input
void SeqDispatch(uint8_t status, uint8_t d1, uint8_t d2, float vel_scale)
{
    MidiRouter::Event e{status, d1, d2};
    router.Dispatch(e, MidiRouter::Source::Playback, clk.NowTick(), vel_scale);
}

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
            if(tb.preroll[tick_i])
            {
                // Count-in: click only (forced — this is the whole point),
                // higher pitch on the first click; no track playback yet
                if(Clock::Engine::OnBeat(t))
                {
                    metro.TriggerBeat(t == 0, true);
                }
            }
            else
            {
                if(Clock::Engine::OnBeat(t))
                {
                    metro.TriggerBeat(Clock::Engine::OnBar(t));
                }
                SeqTrack::ProcessTick(registry, mixer, t, SeqDispatch);
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

// libDaisy's CDC_Transmit_FS/HS dereference the CDC class pointer with NO
// null check (usbd_cdc_if.c), and that pointer only exists while a host
// has the port CONFIGURED. Transmitting on an unplugged/unconfigured port
// is a null-deref -> hard fault -> audio DMA loops its last buffer as a
// constant tone. Guard on dev_state before every transmit.
#include "usbd_def.h"
extern "C" {
extern USBD_HandleTypeDef hUsbDeviceFS; // Seed onboard port (FS_INTERNAL)
extern USBD_HandleTypeDef hUsbDeviceHS; // Pod port, ext pins (FS_EXTERNAL)
}

// ---------------------------------------------------------------------------
// TX queue: protocol frames were fired at the CDC ports fire-and-forget,
// and any frame arriving while the port's previous transfer was still in
// flight silently vanished — bursts (track-data chunks, snapshots)
// dropped their tails ("tracks stuck at loading"). Frames now queue here
// and drain in the main loop with busy-retry per port.
// ---------------------------------------------------------------------------

struct TxFrame
{
    uint16_t len;
    bool     done_int;
    bool     done_ext;
    uint16_t tries;
    uint8_t  data[Protocol::MAX_MESSAGE];
};

constexpr size_t   TX_QUEUE_FRAMES = 64;
constexpr uint16_t TX_MAX_TRIES    = 2000; // ~2 s of main-loop retries
static TxFrame     tx_queue[TX_QUEUE_FRAMES];
static size_t      tx_head = 0; // enqueue position
static size_t      tx_tail = 0; // drain position
static uint32_t    tx_dropped = 0;

// Main-loop context only (enqueue and drain share the thread)
void UsbSendRaw(const uint8_t* data, size_t len)
{
    size_t next = (tx_head + 1) % TX_QUEUE_FRAMES;
    if(next == tx_tail)
    {
        tx_dropped++; // queue full — counted, surfaced via debug stats
        return;
    }
    TxFrame& f = tx_queue[tx_head];
    f.len      = (uint16_t)len;
    f.done_int = false;
    f.done_ext = false;
    f.tries    = 0;
    memcpy(f.data, data, len);
    tx_head = next;
}

void DrainTxQueue()
{
    // A few frames per pass keeps latency low without hogging the loop
    for(int budget = 0; budget < 4 && tx_tail != tx_head; budget++)
    {
        TxFrame& f = tx_queue[tx_tail];

        if(!f.done_int)
        {
            if(hUsbDeviceFS.dev_state != USBD_STATE_CONFIGURED)
            {
                f.done_int = true; // no host on this port: skip
            }
            else if(hw.seed.usb_handle.TransmitInternal(f.data, f.len)
                    == UsbHandle::Result::OK)
            {
                f.done_int = true;
            }
        }
        if(!f.done_ext)
        {
            if(hUsbDeviceHS.dev_state != USBD_STATE_CONFIGURED)
            {
                f.done_ext = true;
            }
            else if(hw.seed.usb_handle.TransmitExternal(f.data, f.len)
                    == UsbHandle::Result::OK)
            {
                f.done_ext = true;
            }
        }

        if(f.done_int && f.done_ext)
        {
            tx_tail = (tx_tail + 1) % TX_QUEUE_FRAMES;
        }
        else if(++f.tries > TX_MAX_TRIES)
        {
            tx_dropped++; // port wedged: don't dam the queue forever
            tx_tail = (tx_tail + 1) % TX_QUEUE_FRAMES;
        }
        else
        {
            break; // port busy: retry this frame next pass, keep order
        }
    }
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
    ui.Transport(clk.Playing(), clk.Locked(), clk.Bpm(), clk.InPreroll());
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
void SendTrack(int slot);
void SendTrackData(int slot);

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
    for(int i = 0; i < Track::MAX_TRACKS; i++)
    {
        if(registry.Get(i).active.load())
        {
            SendTrack(i);
            SendTrackData(i);
        }
    }
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
// Track / capture (main-loop context)
// ---------------------------------------------------------------------------

void SendTrack(int slot)
{
    const Track::Slot&  s  = registry.Get(slot);
    const Mixer::Strip& st = mixer.Get(slot);
    uint8_t p[11];
    p[0]  = (uint8_t)slot;
    p[1]  = s.gen;
    p[2]  = (uint8_t)s.kind;
    p[3]  = s.length_bars;
    p[4]  = st.mute ? 1 : 0;
    p[5]  = Mixer::GainToCc(st.gain);
    p[6]  = Mixer::PanToCc(st.pan);
    p[7]  = Mixer::GainToCc(st.send_rev);
    p[8]  = Mixer::GainToCc(st.send_dly);
    p[9]  = s.created_seq & 0xFF;
    p[10] = (s.created_seq >> 8) & 0xFF;
    ui.Send(Protocol::MSG_TRACK, p, 11);
}

void SendTrackGone(uint8_t slot, uint8_t gen, uint8_t reason)
{
    uint8_t p[3] = {slot, gen, reason};
    ui.Send(Protocol::MSG_TRACK_GONE, p, 3);
}

void SendCaptureMsg(uint8_t status, uint8_t source, uint8_t bars,
                    uint8_t slot, uint8_t gen, uint8_t reason)
{
    uint8_t p[6] = {status, source, bars, slot, gen, reason};
    ui.Send(Protocol::MSG_CAPTURE, p, 6);
}

// Chunked note dump so the arrange view can draw what was captured.
// 5 bytes/event, 50 events/chunk fits MAX_PAYLOAD with the 4-byte header.
void SendTrackData(int slot)
{
    const Track::Slot& s = registry.Get(slot);
    constexpr uint16_t EVENTS_PER_CHUNK = 50;
    uint8_t chunk_count
        = (uint8_t)((s.event_count + EVENTS_PER_CHUNK - 1) / EVENTS_PER_CHUNK);
    if(chunk_count == 0)
        chunk_count = 1; // empty track still announces itself

    for(uint8_t c = 0; c < chunk_count; c++)
    {
        uint8_t  payload[4 + EVENTS_PER_CHUNK * 5];
        size_t   idx   = 0;
        payload[idx++] = (uint8_t)slot;
        payload[idx++] = s.gen;
        payload[idx++] = c;
        payload[idx++] = chunk_count;

        uint16_t start = c * EVENTS_PER_CHUNK;
        uint16_t end   = start + EVENTS_PER_CHUNK;
        if(end > s.event_count)
            end = s.event_count;
        for(uint16_t i = start; i < end; i++)
        {
            const Track::MidiEv& ev = s.events[i];
            payload[idx++]          = ev.tick & 0xFF;
            payload[idx++]          = (ev.tick >> 8) & 0xFF;
            payload[idx++]          = ev.status;
            payload[idx++]          = ev.d1;
            payload[idx++]          = ev.d2;
        }
        ui.Send(Protocol::MSG_TRACK_DATA, payload, (uint16_t)idx);
    }
}

// Rolling-buffer visibility: how many bars back a capture could reach,
// and whether the source played within the last beat.
// "How far back could a grab reach?" — measured from when the clock
// started running (the box listens whether or not you play; an empty
// window is refused at capture time with its own message)
static uint8_t RingSpanBars(const Capture::MidiRing&)
{
    if(!clk.Playing() || clk.InPreroll())
        return 0;
    uint32_t now = clk.NowTick();
    uint32_t run = clk.RunStartTick();
    if(now <= run)
        return 0;
    uint32_t bars = (now - run) / Clock::TICKS_PER_BAR;
    return bars > Capture::MAX_BARS ? Capture::MAX_BARS : (uint8_t)bars;
}

static uint8_t RingActive(const Capture::MidiRing& ring)
{
    uint32_t head = ring.Head();
    if(head == 0)
        return 0;
    uint32_t last = ring.At(head - 1).tick;
    uint32_t now  = clk.NowTick();
    return (now >= last && now - last < Clock::PPQN) ? 1 : 0;
}

void SendSrcActivity()
{
    uint8_t p[4] = {RingSpanBars(pads_ring), RingActive(pads_ring),
                    RingSpanBars(keys_ring), RingActive(keys_ring)};
    ui.Send(Protocol::MSG_SRC_ACTIVITY, p, 4);
}

static uint8_t SnapBars(uint8_t bars)
{
    // Loop lengths are 1/2/4/8 (SPEC.md track model)
    if(bars <= 1) return 1;
    if(bars <= 2) return 2;
    if(bars <= 5) return 4;
    return 8;
}

void CommitCapture(uint8_t source, uint8_t bars, uint32_t end_tick,
                   bool silent_if_empty)
{
    Capture::MidiRing& ring = source == Protocol::SRC_PADS ? pads_ring
                                                           : keys_ring;
    Track::Kind kind = source == Protocol::SRC_PADS ? Track::Kind::MidiDrum
                                                    : Track::Kind::MidiSynth;

    int slot = registry.Create(kind, bars);
    if(slot < 0)
    {
        SendCaptureMsg(Protocol::CAP_REFUSED, source, bars, 0, 0,
                       Protocol::ERR_KIND_CAP);
        ui.Error(Protocol::ERR_KIND_CAP, source);
        return;
    }

    Track::Slot& s = registry.Get(slot);
    auto res = Capture::ExtractWindow(ring, end_tick, bars, s.events,
                                      Track::MAX_EVENTS, s.event_count);
    if(res == Capture::ExtractResult::Empty)
    {
        registry.Abort(slot);
        if(!silent_if_empty)
        {
            SendCaptureMsg(Protocol::CAP_REFUSED, source, bars, 0, 0,
                           Protocol::ERR_EMPTY);
        }
        return;
    }
    if(res == Capture::ExtractResult::Overrun)
    {
        registry.Abort(slot);
        SendCaptureMsg(Protocol::CAP_REFUSED, source, bars, 0, 0,
                       Protocol::ERR_BUSY);
        return;
    }

    mixer.Get(slot).Reset(0.8f);
    registry.Activate(slot);
    SendTrack(slot);
    SendTrackData(slot);
    SendCaptureMsg(Protocol::CAP_COMMITTED, source, bars, (uint8_t)slot,
                   s.gen, 0);
}

void RequestCapture(uint8_t source, uint8_t bars_arg)
{
    if(!clk.Playing())
    {
        ui.Error(Protocol::ERR_NOT_PLAYING, source);
        SendCaptureMsg(Protocol::CAP_REFUSED, source, 0, 0, 0,
                       Protocol::ERR_NOT_PLAYING);
        return;
    }

    bool any = source == Protocol::SRC_ANY;
    for(uint8_t src = Protocol::SRC_PADS; src <= Protocol::SRC_KEYS; src++)
    {
        if(!any && src != source)
            continue;

        uint8_t bars =
            SnapBars(bars_arg != 0 ? bars_arg : src_len_preset[src]);
        uint32_t end = Capture::WindowEndTick(clk.NowTick());

        if(end < (uint32_t)bars * Clock::TICKS_PER_BAR)
        {
            if(!any)
            {
                SendCaptureMsg(Protocol::CAP_REFUSED, src, bars, 0, 0,
                               Protocol::ERR_NO_HISTORY);
                ui.Error(Protocol::ERR_NO_HISTORY, src);
            }
            continue;
        }

        if(end > clk.NowTick())
        {
            pending_cap[src].armed    = true;
            pending_cap[src].bars     = bars;
            pending_cap[src].end_tick = end;
            pending_silent[src]       = any;
            SendCaptureMsg(Protocol::CAP_PENDING, src, bars, 0, 0, 0);
        }
        else
        {
            CommitCapture(src, bars, end, any);
        }
    }
}

/** Main-loop poll: fire pending captures when the clock crosses them. */
void PollPendingCaptures()
{
    for(uint8_t src = 0; src < 2; src++)
    {
        if(!pending_cap[src].armed)
            continue;
        if(!clk.Playing())
        {
            pending_cap[src].armed = false; // stopped while pending
            continue;
        }
        if(clk.NowTick() >= pending_cap[src].end_tick)
        {
            pending_cap[src].armed = false;
            CommitCapture(src, pending_cap[src].bars,
                          pending_cap[src].end_tick, pending_silent[src]);
        }
    }
}

void DoRewind()
{
    clk.Rewind();
    synth.AllNotesOff();
    // The rings hold events stamped with pre-rewind (higher) ticks; once
    // the clock passes those values again they would bleed into capture
    // windows as ghosts from the previous pass. Flush on every rewind.
    pads_ring.Reset();
    keys_ring.Reset();
    pending_cap[Protocol::SRC_PADS].armed = false;
    pending_cap[Protocol::SRC_KEYS].armed = false;
}

void DoUndo()
{
    int slot = registry.NewestActive();
    if(slot < 0)
    {
        return; // nothing to undo — silence, not an error
    }
    uint8_t gen = registry.Get(slot).gen;
    registry.Destroy(slot);
    SendTrackGone((uint8_t)slot, gen, 1);
}

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
        case TARGET_CAPTURE_LEN_PADS:
            src_len_preset[Protocol::SRC_PADS] = 1 << (cc_value / 32);
            break;
        case TARGET_CAPTURE_LEN_KEYS:
            src_len_preset[Protocol::SRC_KEYS] = 1 << (cc_value / 32);
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
            DoRewind();
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

        case Protocol::CMD_CAPTURE:
            if(parser.payload_len >= 2)
            {
                RequestCapture(parser.payload[0], parser.payload[1]);
            }
            break;

        case Protocol::CMD_UNDO:
            DoUndo();
            break;

        case Protocol::CMD_TRACK_DELETE:
            if(parser.payload_len >= 2)
            {
                uint8_t slot = parser.payload[0];
                uint8_t gen  = parser.payload[1];
                if(slot < Track::MAX_TRACKS
                   && registry.Get(slot).active.load()
                   && registry.Get(slot).gen == gen)
                {
                    registry.Destroy(slot);
                    SendTrackGone(slot, gen, 0);
                }
            }
            break;

        case Protocol::CMD_SRC_LEN:
            if(parser.payload_len >= 2 && parser.payload[0] < 3)
            {
                src_len_preset[parser.payload[0]]
                    = SnapBars(parser.payload[1]);
            }
            break;

        case Protocol::CMD_REQ_TRACK_DATA:
            if(parser.payload_len >= 2)
            {
                uint8_t slot = parser.payload[0];
                if(slot < Track::MAX_TRACKS
                   && registry.Get(slot).active.load()
                   && registry.Get(slot).gen == parser.payload[1])
                {
                    SendTrackData(slot);
                }
            }
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

    // The Daisy bootloader ran its own USB DFU on this peripheral moments
    // before jumping here. On macOS the leftover core state made CDC
    // enumeration wedge half-way (descriptors readable, configuration
    // never completing, no serial device created). Force both USB cores
    // through a full RCC reset so CDC init starts from silicon-clean
    // state.
    __HAL_RCC_USB1_OTG_HS_FORCE_RESET();
    __HAL_RCC_USB2_OTG_FS_FORCE_RESET();
    System::Delay(10);
    __HAL_RCC_USB1_OTG_HS_RELEASE_RESET();
    __HAL_RCC_USB2_OTG_FS_RELEASE_RESET();
    System::Delay(10);

    // USB CDC on BOTH ports: the Seed's onboard micro-USB (FS_INTERNAL)
    // and the Pod's own micro-USB connector, which is wired to the Seed's
    // external USB pins 37/38 (FS_EXTERNAL). Companion can connect to
    // either; power can come from either.
    hw.seed.usb_handle.Init(UsbHandle::FS_BOTH);
    hw.seed.usb_handle.SetReceiveCallback(UsbReceiveCallback,
                                          UsbHandle::FS_BOTH);

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
    router.SetRecordHook(RecordToRings);
    registry.Init();
    pads_ring.Reset();
    keys_ring.Reset();
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
                DoRewind();
                ui.Sync(clk.NowTick());
            }
            else
            {
                clk.Play();
            }
            SendTransport();
        }

        // Button 2: undo — delete the newest capture (SPEC.md undo model)
        if(hw.button2.RisingEdge())
        {
            DoUndo();
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

        // Drain queued protocol frames (busy-retry per port)
        DrainTxQueue();

        // Pending retrospective captures fire when the clock crosses
        // their bar boundary
        PollPendingCaptures();

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

        // Rolling-buffer visibility for the capture history rings (~4 Hz)
        static uint32_t last_activity_send = 0;
        if(clk.Playing() && now - last_activity_send >= 250)
        {
            last_activity_send = now;
            SendSrcActivity();
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

                    // KeyLab Live button (CC 3, main port) = Capture.
                    // The KeyLab transport buttons only exist on the DAW
                    // USB port and never reach the DIN input, so Live is
                    // the one spare main-port button (keylab_essential.md)
                    if(cc.control_number == 3)
                    {
                        RequestCapture(Protocol::SRC_ANY, 0);
                        break;
                    }

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
            if(clk.InPreroll())
            {
                // Count-in: amber pulse — "get ready"
                hw.led1.Set(1.0f, 0.5f, 0.0f);
            }
            else if(clk.Playing())
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
