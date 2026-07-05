#pragma once
#ifndef GROOVYDAISY_METRONOME_H
#define GROOVYDAISY_METRONOME_H

#include <stdint.h>
#include <math.h>

/**
 * Synthesized click metronome. Bar downbeats get a higher pitch.
 * Mixed through its own strip (Mixer::STRIP_METRO); level control there.
 * Essential for the guitarist to find the grid before any drums exist
 * (SPEC.md, transport & timing).
 */

namespace Metronome
{

class Engine
{
  public:
    void Init(float sample_rate)
    {
        sample_rate_ = sample_rate;
        enabled_     = false;
        phase_       = 0.0f;
        phase_inc_   = 0.0f;
        env_         = 0.0f;
        env_decay_   = expf(-1.0f / (0.015f * sample_rate)); // ~15 ms click
    }

    void SetEnabled(bool on) { enabled_ = on; }
    bool Enabled() const { return enabled_; }

    /** Call on beat ticks (audio callback). `force` bypasses the enable
     *  switch — the count-in must click even with the metronome off. */
    void TriggerBeat(bool bar_downbeat, bool force = false)
    {
        if(!enabled_ && !force)
            return;
        float freq = bar_downbeat ? 1760.0f : 1175.0f; // A6 / D6-ish
        phase_inc_ = freq / sample_rate_;
        phase_     = 0.0f;
        env_       = 1.0f;
    }

    /** Render one sample. */
    inline float Process()
    {
        if(env_ < 0.001f)
            return 0.0f;
        phase_ += phase_inc_;
        if(phase_ >= 1.0f)
            phase_ -= 1.0f;
        float s = sinf(phase_ * 6.2831853f) * env_;
        env_ *= env_decay_;
        return s;
    }

  private:
    float sample_rate_;
    bool  enabled_;
    float phase_;
    float phase_inc_;
    float env_;
    float env_decay_;
};

} // namespace Metronome

#endif // GROOVYDAISY_METRONOME_H
