#pragma once
#ifndef GROOVYDAISY_FX_H
#define GROOVYDAISY_FX_H

#include <stdint.h>
#include <cmath>
#include "daisysp.h"

/**
 * GroovyDaisy send FX (Phase 5): one shared reverb + one tempo-synced
 * ping-pong delay. "FX are core scope, not later — half the campfire
 * vibe is the space" (SPEC.md audio engine).
 *
 * Both are SEND effects: the mixer accumulates mono post-fader send
 * buses (mixer.h Bus) and Process() adds the wet returns to the master
 * bus. The reverb runs every sample regardless of input — tails must
 * ring out after the send stops.
 *
 * The delay is clocked in SIXTEENTHS, not seconds: the division stays
 * musical at any tempo, and when tempo does change (it locks once audio
 * loops exist) the delay time slews there over ~50 ms — a brief tape-ish
 * pitch bend instead of a click.
 *
 * NOT host-compilable (DaisySP: ReverbSc from DaisySP-LGPL + DelayLine),
 * same rule as synth.h. The whole engine lives in SDRAM (main.cpp owns
 * the DSY_SDRAM_BSS instance); Init() zeroes every buffer it touches, so
 * unzeroed SDRAM BSS is safe.
 */

namespace Fx
{

// 2 s of delay line: a half note at 60 BPM with headroom
constexpr size_t DELAY_MAX = 96000;

// Delay divisions in 16th notes: 1/8, dotted 1/8, 1/4, dotted 1/4, 1/2.
// Dotted 1/8 is the default — THE campfire delay.
constexpr uint8_t DIV_16THS[] = {2, 3, 4, 6, 8};
constexpr uint8_t NUM_DIVS    = 5;

class Engine
{
  public:
    /**
     * NO default member initializers anywhere in this class: the
     * instance lives in DSY_SDRAM_BSS, and an NSDMI forces a global
     * constructor that writes to SDRAM during static init — BEFORE
     * hw.Init() configures the FMC. That is a hard fault at boot (found
     * the hard way, 2026-07-06: the box enumerated nothing). Everything
     * is initialized here, at runtime, after SDRAM exists.
     */
    void Init(float sample_rate)
    {
        sample_rate_ = sample_rate;
        verb_.Init(sample_rate);
        dly_l_.Init();
        dly_r_.Init();

        // Defaults tuned by ear-in-advance; the feel test refines them
        SetRevSize(88); // feedback ~0.86: a generous room, not a cave
        SetRevTone(96); // ~8 kHz lowpass: warm, not dull
        SetDelayDiv(1); // dotted 8th
        SetDelayFb(55); // two or three audible repeats

        float s16       = sample_rate * 0.125f; // 16th @ 120 BPM
        delay_samples_  = s16 * DIV_16THS[div_idx_];
        delay_target_   = delay_samples_;
        dly_l_.SetDelay(delay_samples_);
        dly_r_.SetDelay(delay_samples_);
    }

    // --- parameter setters (main loop; float/byte writes don't tear) ----

    void SetRevSize(uint8_t cc)
    {
        rev_size_cc_ = cc;
        // 0.60 (tight) .. 0.97 (long) — ReverbSc goes unstable near 1.0
        verb_.SetFeedback(0.60f + ((float)cc / 127.0f) * 0.37f);
    }

    void SetRevTone(uint8_t cc)
    {
        rev_tone_cc_ = cc;
        // 800 Hz .. 16 kHz, log taper
        verb_.SetLpFreq(800.0f * powf(20.0f, (float)cc / 127.0f));
    }

    void SetDelayDiv(uint8_t idx)
    {
        div_idx_ = idx < NUM_DIVS ? idx : NUM_DIVS - 1;
    }

    void SetDelayFb(uint8_t cc)
    {
        dly_fb_cc_ = cc;
        dly_fb_    = ((float)cc / 127.0f) * 0.85f; // <1: always dies out
    }

    uint8_t RevSizeCc() const { return rev_size_cc_; }
    uint8_t RevToneCc() const { return rev_tone_cc_; }
    uint8_t DelayDiv() const { return div_idx_; }
    uint8_t DelayFbCc() const { return dly_fb_cc_; }

    /** Audio callback, once per block: retarget the tempo-synced time. */
    inline void UpdateTempo(float samples_per_tick)
    {
        // 24 ticks per 16th (clock.h PPQN 96)
        float t = samples_per_tick * 24.0f * (float)DIV_16THS[div_idx_];
        if(t > (float)(DELAY_MAX - 2))
            t = (float)(DELAY_MAX - 2);
        delay_target_ = t;
    }

    /**
     * Audio callback, once per sample: consume the mono send buses and
     * add the wet returns to the master bus.
     */
    inline void Process(float rev_in, float dly_in, float& l, float& r)
    {
        // Slew the delay time (~50 ms) — tempo changes bend, never click
        delay_samples_ += 0.0004f * (delay_target_ - delay_samples_);
        dly_l_.SetDelay(delay_samples_);
        dly_r_.SetDelay(delay_samples_);

        // Ping-pong: input enters left; each repeat crosses sides
        float dl = dly_l_.Read();
        float dr = dly_r_.Read();
        dly_l_.Write(dly_in + dr * dly_fb_);
        dly_r_.Write(dl * dly_fb_);

        float wl, wr;
        verb_.Process(rev_in, rev_in, &wl, &wr);

        l += wl + dl;
        r += wr + dr;
    }

  private:
    float sample_rate_;

    daisysp::ReverbSc verb_;
    daisysp::DelayLine<float, DELAY_MAX> dly_l_;
    daisysp::DelayLine<float, DELAY_MAX> dly_r_;

    float   delay_samples_;
    float   delay_target_;
    float   dly_fb_;
    uint8_t div_idx_;
    uint8_t rev_size_cc_;
    uint8_t rev_tone_cc_;
    uint8_t dly_fb_cc_;
};

} // namespace Fx

#endif // GROOVYDAISY_FX_H
