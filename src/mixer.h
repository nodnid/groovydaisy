#pragma once
#ifndef GROOVYDAISY_MIXER_H
#define GROOVYDAISY_MIXER_H

#include <stdint.h>
#include <cmath>

/**
 * GroovyDaisy mixer (v2).
 *
 * One Strip per mixable source. Track strips (0..31) map 1:1 to the track
 * registry slots (Phase 2+); the fixed strips carry the live instrument:
 * guitar pass-through, synth engine, drum engine, metronome.
 *
 * MIDI-backed tracks don't produce separable audio (shared engines), so
 * their strips are interpreted at MIDI dispatch time (level -> velocity
 * scale, mute -> suppression); audio strips are applied per sample here.
 * The companion shows identical strips either way (SPEC.md, decision 7).
 *
 * Phase 5: Add*() accumulates into a Bus carrying the stereo mix AND two
 * mono post-fader FX send buses (post-fader so muting a strip silences
 * its reverb feed too). Per-strip peaks accumulate here for the 10 Hz
 * meter publish: the callback maxes, the main loop reads-and-clears —
 * unsynchronized by design (a lost max between clear and next write is
 * one meter frame of underread, invisible; floats don't tear on M7).
 */

namespace Mixer
{

constexpr int NUM_TRACK_STRIPS = 32;
constexpr int STRIP_GUITAR     = 32;
constexpr int STRIP_SYNTH      = 33;
constexpr int STRIP_DRUMS      = 34;
constexpr int STRIP_METRO      = 35;
constexpr int NUM_STRIPS       = 36;

struct Strip
{
    float gain;     // 0..1 linear
    float pan;      // -1 (L) .. 0 (C) .. +1 (R)
    float send_rev; // 0..1
    float send_dly; // 0..1
    bool  mute;

    void Reset(float g = 0.8f)
    {
        gain     = g;
        pan      = 0.0f;
        send_rev = 0.0f;
        send_dly = 0.0f;
        mute     = false;
    }
};

/** One audio block's accumulation target: stereo mix + mono FX sends. */
struct Bus
{
    float l, r;
    float rev, dly;

    void Clear() { l = r = rev = dly = 0.0f; }
};

class Engine
{
  public:
    void Init()
    {
        for(int i = 0; i < NUM_STRIPS; i++)
        {
            strips_[i].Reset();
            peaks_[i] = 0.0f;
        }
        strips_[STRIP_METRO].gain = 0.5f;
        master_                   = 0.85f;
        peak_master_l_            = 0.0f;
        peak_master_r_            = 0.0f;
    }

    Strip&       Get(int idx) { return strips_[idx]; }
    const Strip& Get(int idx) const { return strips_[idx]; }

    void  SetMaster(float g) { master_ = g; }
    float Master() const { return master_; }

    /**
     * Sum a mono source through its strip into the bus (mix + sends).
     * Equal-gain linear pan (matches v1 sampler behavior). Sends are
     * post-fader and mono.
     */
    inline void AddMono(int strip, float in, Bus& bus)
    {
        const Strip& s = strips_[strip];
        if(s.mute)
            return;
        float g = in * s.gain;
        // pan <= 0: full left, attenuate right; pan >= 0: mirror
        bus.l += g * (s.pan <= 0.0f ? 1.0f : 1.0f - s.pan);
        bus.r += g * (s.pan >= 0.0f ? 1.0f : 1.0f + s.pan);
        bus.rev += g * s.send_rev;
        bus.dly += g * s.send_dly;
        float a = g < 0 ? -g : g;
        if(a > peaks_[strip])
            peaks_[strip] = a;
    }

    /** Sum a stereo source through its strip (pan acts as balance). */
    inline void AddStereo(int strip, float in_l, float in_r, Bus& bus)
    {
        const Strip& s = strips_[strip];
        if(s.mute)
            return;
        float gl = in_l * s.gain * (s.pan <= 0.0f ? 1.0f : 1.0f - s.pan);
        float gr = in_r * s.gain * (s.pan >= 0.0f ? 1.0f : 1.0f + s.pan);
        bus.l += gl;
        bus.r += gr;
        float mono = (in_l + in_r) * 0.5f * s.gain;
        bus.rev += mono * s.send_rev;
        bus.dly += mono * s.send_dly;
        float a = gl < 0 ? -gl : gl;
        float b = gr < 0 ? -gr : gr;
        if(b > a)
            a = b;
        if(a > peaks_[strip])
            peaks_[strip] = a;
    }

    /** Apply master gain to the finished bus and track master peaks. */
    inline void ApplyMaster(float& l, float& r)
    {
        l *= master_;
        r *= master_;
        float a = l < 0 ? -l : l;
        float b = r < 0 ? -r : r;
        if(a > peak_master_l_)
            peak_master_l_ = a;
        if(b > peak_master_r_)
            peak_master_r_ = b;
    }

    // --- Meters (main loop: read at ~10 Hz, then clear) -----------------

    float ReadPeak(int strip)
    {
        float p       = peaks_[strip];
        peaks_[strip] = 0.0f;
        return p;
    }

    void ReadMasterPeaks(float& l, float& r)
    {
        l              = peak_master_l_;
        r              = peak_master_r_;
        peak_master_l_ = 0.0f;
        peak_master_r_ = 0.0f;
    }

  private:
    Strip strips_[NUM_STRIPS];
    float master_;
    float peaks_[NUM_STRIPS];
    float peak_master_l_;
    float peak_master_r_;
};

// --- CC-scale (0-127) conversions for protocol/control surface ----------

inline float CcToGain(uint8_t v)
{
    return (float)v / 127.0f;
}

inline uint8_t GainToCc(float g)
{
    if(g < 0.0f)
        g = 0.0f;
    if(g > 1.0f)
        g = 1.0f;
    return (uint8_t)(g * 127.0f + 0.5f);
}

/** Peak -> meter byte with a square-root taper so quiet material still
 *  moves the bar (linear meters sit dark below -12 dB). */
inline uint8_t PeakToCc(float p)
{
    if(p <= 0.0f)
        return 0;
    if(p > 1.0f)
        p = 1.0f;
    return (uint8_t)(sqrtf(p) * 127.0f + 0.5f);
}

inline float CcToPan(uint8_t v)
{
    return ((float)v - 64.0f) / 64.0f; // 0 -> -1, 64 -> 0, 127 -> ~+0.98
}

inline uint8_t PanToCc(float p)
{
    if(p < -1.0f)
        p = -1.0f;
    if(p > 1.0f)
        p = 1.0f;
    float v = p * 64.0f + 64.0f;
    if(v > 127.0f)
        v = 127.0f;
    return (uint8_t)(v + 0.5f);
}

} // namespace Mixer

#endif // GROOVYDAISY_MIXER_H
