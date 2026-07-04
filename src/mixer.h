#pragma once
#ifndef GROOVYDAISY_MIXER_H
#define GROOVYDAISY_MIXER_H

#include <stdint.h>

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
 * FX sends are stored from Phase 1 but only become audible in Phase 5.
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
    float send_rev; // 0..1 (audible from Phase 5)
    float send_dly; // 0..1 (audible from Phase 5)
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

class Engine
{
  public:
    void Init()
    {
        for(int i = 0; i < NUM_STRIPS; i++)
        {
            strips_[i].Reset();
        }
        strips_[STRIP_METRO].gain = 0.5f;
        master_                   = 0.85f;
    }

    Strip&       Get(int idx) { return strips_[idx]; }
    const Strip& Get(int idx) const { return strips_[idx]; }

    void  SetMaster(float g) { master_ = g; }
    float Master() const { return master_; }

    /**
     * Sum a mono source through its strip into the stereo bus.
     * Equal-gain linear pan (matches v1 sampler behavior).
     */
    inline void AddMono(int strip, float in, float& l, float& r) const
    {
        const Strip& s = strips_[strip];
        if(s.mute)
            return;
        float g = in * s.gain;
        // pan <= 0: full left, attenuate right; pan >= 0: mirror
        l += g * (s.pan <= 0.0f ? 1.0f : 1.0f - s.pan);
        r += g * (s.pan >= 0.0f ? 1.0f : 1.0f + s.pan);
    }

    /** Sum a stereo source through its strip (pan acts as balance). */
    inline void AddStereo(int strip, float in_l, float in_r, float& l,
                          float& r) const
    {
        const Strip& s = strips_[strip];
        if(s.mute)
            return;
        l += in_l * s.gain * (s.pan <= 0.0f ? 1.0f : 1.0f - s.pan);
        r += in_r * s.gain * (s.pan >= 0.0f ? 1.0f : 1.0f + s.pan);
    }

    /** Apply master gain to the finished bus. */
    inline void ApplyMaster(float& l, float& r) const
    {
        l *= master_;
        r *= master_;
    }

  private:
    Strip strips_[NUM_STRIPS];
    float master_;
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
