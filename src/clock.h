#pragma once
#ifndef GROOVYDAISY_CLOCK_H
#define GROOVYDAISY_CLOCK_H

#include <stdint.h>
#include <stddef.h>

/**
 * GroovyDaisy master clock (v2).
 *
 * One monotonic tick counter at 96 PPQN drives everything: track playback
 * position is `NowTick() % track_length`, so same-length tracks are always
 * phase-aligned (SPEC.md, track model). There is no pattern wrap — the
 * pattern concept is gone in v2.
 *
 * Keeps v1 transport.h's float-accumulator tick math (verified to <2 ticks
 * drift/minute by test_clock_math.cpp). Adds tap tempo and a block-oriented
 * Advance() for the audio callback. Tempo locking (audio loops present) is
 * enforced here; the lock is owned by the audio-track engine from Phase 3.
 */

namespace Clock
{

constexpr uint32_t PPQN          = 96;
constexpr uint32_t BEATS_PER_BAR = 4;
constexpr uint32_t TICKS_PER_BAR = PPQN * BEATS_PER_BAR; // 384
constexpr float    DEFAULT_BPM   = 120.0f;
constexpr float    MIN_BPM       = 60.0f;
constexpr float    MAX_BPM       = 200.0f;

// Tap tempo: taps further apart than this start a new tap sequence.
constexpr uint32_t TAP_TIMEOUT_MS = 2000;
constexpr int      TAP_HISTORY    = 4;

struct TickBlock
{
    static constexpr size_t MAX_TICKS = 16;
    uint32_t tick[MAX_TICKS];   // absolute tick numbers
    size_t   frame[MAX_TICKS];  // frame offset within the block
    size_t   count;
};

class Engine
{
  public:
    void Init(float sample_rate)
    {
        sample_rate_ = sample_rate;
        playing_     = false;
        locked_      = false;
        bpm_         = DEFAULT_BPM;
        tick_        = 0;
        accumulator_ = 0.0f;
        tap_count_   = 0;
        last_tap_ms_ = 0;
        dirty_       = true;
        UpdateTickInterval();
    }

    /**
     * Advance the clock by one audio block. Call once per callback.
     * Fills `out` with every tick that occurs in the block and its frame
     * offset (for sample-accurate event dispatch).
     */
    void Advance(size_t nframes, TickBlock& out)
    {
        out.count = 0;
        if(!playing_)
        {
            return;
        }
        for(size_t f = 0; f < nframes; f++)
        {
            accumulator_ += 1.0f;
            if(accumulator_ >= samples_per_tick_)
            {
                accumulator_ -= samples_per_tick_;
                tick_++;
                if(out.count < TickBlock::MAX_TICKS)
                {
                    out.tick[out.count]  = tick_;
                    out.frame[out.count] = f;
                    out.count++;
                }
            }
        }
    }

    // --- Transport -------------------------------------------------------

    void Play()
    {
        if(!playing_)
        {
            playing_ = true;
            dirty_   = true;
        }
    }

    void Stop()
    {
        if(playing_)
        {
            playing_ = false;
            dirty_   = true;
        }
    }

    /** Stop + return to tick 0 (double-tap stop). Never clears content. */
    void Rewind()
    {
        playing_     = false;
        tick_        = 0;
        accumulator_ = 0.0f;
        dirty_       = true;
    }

    // --- Tempo -----------------------------------------------------------

    /** Returns false if refused (tempo locked). */
    bool SetBpm(float bpm)
    {
        if(locked_)
        {
            return false;
        }
        if(bpm < MIN_BPM)
            bpm = MIN_BPM;
        if(bpm > MAX_BPM)
            bpm = MAX_BPM;
        if(bpm != bpm_)
        {
            bpm_   = bpm;
            dirty_ = true;
            UpdateTickInterval();
        }
        return true;
    }

    bool AdjustBpm(float delta) { return SetBpm(bpm_ + delta); }

    /**
     * Register a tap at `now_ms`. Averages the last TAP_HISTORY intervals.
     * Returns false if refused (tempo locked). A single tap (no interval
     * yet) succeeds but changes nothing.
     */
    bool Tap(uint32_t now_ms)
    {
        if(locked_)
        {
            return false;
        }
        if(tap_count_ > 0 && now_ms - last_tap_ms_ > TAP_TIMEOUT_MS)
        {
            tap_count_ = 0; // stale sequence, start over
        }
        if(tap_count_ > 0)
        {
            uint32_t interval = now_ms - last_tap_ms_;
            intervals_[(tap_count_ - 1) % TAP_HISTORY] = interval;
        }
        last_tap_ms_ = now_ms;
        tap_count_++;

        int n = tap_count_ - 1;
        if(n > TAP_HISTORY)
            n = TAP_HISTORY;
        if(n > 0)
        {
            float sum = 0.0f;
            for(int i = 0; i < n; i++)
                sum += (float)intervals_[i];
            float avg_ms = sum / (float)n;
            SetBpm(60000.0f / avg_ms);
        }
        return true;
    }

    /** Tempo lock (audio loops exist). Owned by the audio-track engine. */
    void SetLocked(bool locked)
    {
        if(locked_ != locked)
        {
            locked_ = locked;
            dirty_  = true;
        }
    }

    // --- Queries ---------------------------------------------------------

    bool     Playing() const { return playing_; }
    bool     Locked() const { return locked_; }
    float    Bpm() const { return bpm_; }
    uint32_t NowTick() const { return tick_; }
    float    SampleRate() const { return sample_rate_; }
    float    SamplesPerTick() const { return samples_per_tick_; }
    uint32_t SamplesPerBar() const
    {
        return (uint32_t)(samples_per_tick_ * (float)TICKS_PER_BAR + 0.5f);
    }

    static uint32_t Bar(uint32_t tick) { return tick / TICKS_PER_BAR; }
    static uint32_t BeatInBar(uint32_t tick)
    {
        return (tick % TICKS_PER_BAR) / PPQN;
    }
    static bool OnBeat(uint32_t tick) { return tick % PPQN == 0; }
    static bool OnBar(uint32_t tick) { return tick % TICKS_PER_BAR == 0; }

    /** Nearest bar boundary to `tick` (rounds half up). */
    static uint32_t NearestBar(uint32_t tick)
    {
        return ((tick + TICKS_PER_BAR / 2) / TICKS_PER_BAR) * TICKS_PER_BAR;
    }

    /** Transport/tempo changed since last check (drives MSG_TRANSPORT). */
    bool CheckDirty()
    {
        bool d = dirty_;
        dirty_ = false;
        return d;
    }

  private:
    void UpdateTickInterval()
    {
        float ticks_per_second = (bpm_ * (float)PPQN) / 60.0f;
        samples_per_tick_      = sample_rate_ / ticks_per_second;
    }

    float    sample_rate_;
    bool     playing_;
    bool     locked_;
    float    bpm_;
    uint32_t tick_;
    float    accumulator_;
    float    samples_per_tick_;
    bool     dirty_;

    uint32_t intervals_[TAP_HISTORY];
    uint32_t last_tap_ms_;
    int      tap_count_;
};

} // namespace Clock

#endif // GROOVYDAISY_CLOCK_H
