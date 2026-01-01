#pragma once
#ifndef GROOVYDAISY_TRANSPORT_H
#define GROOVYDAISY_TRANSPORT_H

#include <stdint.h>

/**
 * GroovyDaisy Transport Engine
 *
 * Provides tempo-synchronized timing at PPQN resolution.
 * Handles play/stop/record state and position tracking.
 */

namespace Transport
{

// Timing constants
constexpr uint32_t PPQN           = 96;   // Pulses per quarter note
constexpr uint32_t BEATS_PER_BAR  = 4;    // Time signature: 4/4
constexpr uint32_t TICKS_PER_BAR  = PPQN * BEATS_PER_BAR;
constexpr uint32_t DEFAULT_BPM    = 120;
constexpr uint32_t MIN_BPM        = 30;
constexpr uint32_t MAX_BPM        = 300;
constexpr uint32_t DEFAULT_BARS   = 4;    // Default pattern length

// Transport states
enum class State
{
    STOPPED,
    PLAYING,
    RECORDING
};

/**
 * Transport position in musical time
 */
struct Position
{
    uint32_t tick;   // Total ticks since start (0 to pattern_length-1)
    uint16_t bar;    // Current bar (1-based)
    uint8_t  beat;   // Current beat within bar (1-4)
    uint8_t  pulse;  // Current pulse within beat (0-95)

    void Reset()
    {
        tick  = 0;
        bar   = 1;
        beat  = 1;
        pulse = 0;
    }

    /**
     * Update bar/beat/pulse from tick position
     */
    void UpdateFromTick()
    {
        bar   = (tick / TICKS_PER_BAR) + 1;
        beat  = ((tick % TICKS_PER_BAR) / PPQN) + 1;
        pulse = tick % PPQN;
    }
};

/**
 * Main transport engine
 */
class Engine
{
  public:
    /**
     * Initialize the transport engine
     * @param sample_rate Audio sample rate (e.g., 48000)
     */
    void Init(float sample_rate)
    {
        sample_rate_   = sample_rate;
        state_         = State::STOPPED;
        bpm_           = DEFAULT_BPM;
        pattern_bars_  = DEFAULT_BARS;
        pattern_ticks_ = pattern_bars_ * TICKS_PER_BAR;
        position_.Reset();
        accumulator_    = 0.0f;
        state_changed_  = false;
        pattern_looped_ = false;

        UpdateTickInterval();
    }

    /**
     * Process one audio sample worth of time
     * Call this once per sample in the audio callback
     * @return true if a new tick occurred
     */
    bool Process()
    {
        if(state_ == State::STOPPED)
        {
            return false;
        }

        accumulator_ += 1.0f;

        if(accumulator_ >= samples_per_tick_)
        {
            accumulator_ -= samples_per_tick_;

            // Advance tick
            position_.tick++;

            // Loop at pattern end
            if(position_.tick >= pattern_ticks_)
            {
                position_.tick = 0;
                pattern_looped_ = true;  // Signal that pattern just looped
            }

            position_.UpdateFromTick();
            return true;
        }

        return false;
    }

    // Transport controls
    void Play()
    {
        if(state_ != State::PLAYING)
        {
            state_         = State::PLAYING;
            state_changed_ = true;
        }
    }

    void Stop()
    {
        if(state_ != State::STOPPED)
        {
            state_         = State::STOPPED;
            state_changed_ = true;
            // Don't reset position - allow resume from current position
        }
    }

    void StopAndReset()
    {
        state_         = State::STOPPED;
        state_changed_ = true;
        position_.Reset();
        accumulator_ = 0.0f;
    }

    void Record()
    {
        if(state_ != State::RECORDING)
        {
            if(state_ == State::STOPPED)
            {
                // Start recording from beginning
                position_.Reset();
                accumulator_ = 0.0f;
            }
            state_         = State::RECORDING;
            state_changed_ = true;
        }
    }

    void ToggleRecord()
    {
        if(state_ == State::RECORDING)
        {
            Play();  // Exit record mode but keep playing
        }
        else
        {
            Record();
        }
    }

    // Tempo control
    void SetBpm(uint16_t bpm)
    {
        if(bpm < MIN_BPM)
            bpm = MIN_BPM;
        if(bpm > MAX_BPM)
            bpm = MAX_BPM;

        if(bpm_ != bpm)
        {
            bpm_           = bpm;
            state_changed_ = true;
            UpdateTickInterval();
        }
    }

    void AdjustBpm(int16_t delta)
    {
        int16_t new_bpm = static_cast<int16_t>(bpm_) + delta;
        SetBpm(static_cast<uint16_t>(new_bpm));
    }

    // Pattern length control
    void SetPatternBars(uint8_t bars)
    {
        if(bars < 1)
            bars = 1;
        if(bars > 16)
            bars = 16;

        pattern_bars_  = bars;
        pattern_ticks_ = pattern_bars_ * TICKS_PER_BAR;

        // Wrap current position if beyond new length
        if(position_.tick >= pattern_ticks_)
        {
            position_.tick = position_.tick % pattern_ticks_;
            position_.UpdateFromTick();
        }
    }

    // State queries
    State    GetState() const { return state_; }
    bool     IsPlaying() const { return state_ == State::PLAYING; }
    bool     IsRecording() const { return state_ == State::RECORDING; }
    bool     IsStopped() const { return state_ == State::STOPPED; }
    uint16_t GetBpm() const { return bpm_; }
    uint8_t  GetPatternBars() const { return pattern_bars_; }
    uint32_t GetPatternTicks() const { return pattern_ticks_; }

    const Position& GetPosition() const { return position_; }

    /**
     * Check and clear state changed flag
     * Use this to know when to send TRANSPORT message
     */
    bool CheckStateChanged()
    {
        bool changed   = state_changed_;
        state_changed_ = false;
        return changed;
    }

    /**
     * Check if we're at the start of a beat
     * Useful for visual feedback/sync
     */
    bool IsOnBeat() const { return position_.pulse == 0; }

    /**
     * Check if we're at the start of a bar
     */
    bool IsOnBar() const { return IsOnBeat() && position_.beat == 1; }

    /**
     * Check and clear pattern looped flag
     * Use this to detect when pattern has looped (for freeze finalization)
     */
    bool CheckPatternLooped()
    {
        bool looped     = pattern_looped_;
        pattern_looped_ = false;
        return looped;
    }

  private:
    void UpdateTickInterval()
    {
        // Calculate samples per tick
        // ticks_per_minute = bpm * PPQN
        // samples_per_tick = sample_rate * 60 / ticks_per_minute
        float ticks_per_second = (bpm_ * PPQN) / 60.0f;
        samples_per_tick_      = sample_rate_ / ticks_per_second;
    }

    float    sample_rate_;
    State    state_;
    uint16_t bpm_;
    uint8_t  pattern_bars_;
    uint32_t pattern_ticks_;
    Position position_;
    float    accumulator_;
    float    samples_per_tick_;
    bool     state_changed_;
    bool     pattern_looped_;
};

} // namespace Transport

#endif // GROOVYDAISY_TRANSPORT_H
