#pragma once
#ifndef GROOVYDAISY_SAMPLER_H
#define GROOVYDAISY_SAMPLER_H

#include <stdint.h>
#include <stddef.h>

/**
 * GroovyDaisy Sample-Based Drum Engine
 *
 * 8-voice polyphonic sample playback engine for drum sounds.
 * Samples are stored in SDRAM as float arrays.
 */

namespace Sampler
{

// Constants
constexpr uint8_t NUM_VOICES = 8;
constexpr uint8_t FIRST_PAD_NOTE = 36;  // KeyLab pads start at note 36
constexpr uint8_t LAST_PAD_NOTE = 43;   // 8 pads: 36-43
constexpr uint8_t DRUM_CHANNEL = 9;     // Channel 10 (0-indexed = 9)

/**
 * Sample slot - points to sample data in SDRAM
 */
struct Sample
{
    const float* data;     // Pointer to sample data
    size_t       length;   // Length in samples
    const char*  name;     // Sample name (for debugging)

    void Clear()
    {
        data   = nullptr;
        length = 0;
        name   = nullptr;
    }
};

/**
 * Single drum voice with playback state
 */
struct DrumVoice
{
    const float* sample_data;    // Pointer to sample data
    size_t       sample_length;  // Length in samples
    size_t       play_head;      // Current position
    float        amplitude;      // Current envelope level
    float        decay;          // Envelope decay rate (per sample)
    float        pitch;          // Playback rate (1.0 = normal)
    float        level;          // Track volume (0.0-1.0)
    float        pan;            // Stereo position (-1.0 to +1.0)
    float        velocity;       // Trigger velocity (0.0-1.0)
    bool         playing;        // Is voice active

    void Init()
    {
        sample_data   = nullptr;
        sample_length = 0;
        play_head     = 0;
        amplitude     = 0.0f;
        decay         = 0.9999f;  // Long decay by default
        pitch         = 1.0f;
        level         = 1.0f;
        pan           = 0.0f;     // Center
        velocity      = 1.0f;
        playing       = false;
    }

    /**
     * Trigger the voice with a sample
     */
    void Trigger(const Sample& sample, float vel)
    {
        sample_data   = sample.data;
        sample_length = sample.length;
        play_head     = 0;
        amplitude     = 1.0f;
        velocity      = vel;
        playing       = true;
    }

    /**
     * Process one sample of output
     */
    float Process()
    {
        if(!playing || sample_data == nullptr)
        {
            return 0.0f;
        }

        // Linear interpolation for pitch shifting
        float pos       = static_cast<float>(play_head);
        size_t idx      = static_cast<size_t>(pos);
        float  frac     = pos - static_cast<float>(idx);

        float out = 0.0f;
        if(idx < sample_length)
        {
            out = sample_data[idx];
            // Interpolate with next sample if available
            if(idx + 1 < sample_length)
            {
                out += frac * (sample_data[idx + 1] - out);
            }
        }

        // Apply envelope and volume
        out *= amplitude * level * velocity;

        // Advance playhead
        play_head++;

        // Apply decay envelope
        amplitude *= decay;

        // Stop if reached end of sample or amplitude too low
        if(play_head >= sample_length || amplitude < 0.001f)
        {
            playing = false;
        }

        return out;
    }
};

/**
 * Main sampler engine managing 8 drum voices
 */
class Engine
{
  public:
    /**
     * Initialize the sampler engine
     */
    void Init()
    {
        for(uint8_t i = 0; i < NUM_VOICES; i++)
        {
            voices_[i].Init();
            samples_[i].Clear();
        }
        active_count_ = 0;
        master_level_ = 1.0f;
    }

    /**
     * Load a sample into a slot
     */
    void LoadSample(uint8_t slot, const float* data, size_t length, const char* name = nullptr)
    {
        if(slot >= NUM_VOICES)
            return;

        samples_[slot].data   = data;
        samples_[slot].length = length;
        samples_[slot].name   = name;
    }

    /**
     * Trigger a drum voice by pad number (0-7)
     */
    void Trigger(uint8_t pad, float velocity = 1.0f)
    {
        if(pad >= NUM_VOICES)
            return;

        // Check if sample is loaded
        if(samples_[pad].data == nullptr || samples_[pad].length == 0)
            return;

        // Retrigger: if this pad's voice is already playing, restart it
        voices_[pad].Trigger(samples_[pad], velocity);
    }

    /**
     * Trigger by MIDI note (36-43 on channel 10)
     * Returns true if note was handled
     */
    bool TriggerMidi(uint8_t channel, uint8_t note, uint8_t velocity)
    {
        // Only respond to drum channel (10, 0-indexed = 9)
        if(channel != DRUM_CHANNEL)
            return false;

        // Only respond to pad notes
        if(note < FIRST_PAD_NOTE || note > LAST_PAD_NOTE)
            return false;

        uint8_t pad = note - FIRST_PAD_NOTE;
        float   vel = static_cast<float>(velocity) / 127.0f;

        Trigger(pad, vel);
        return true;
    }

    /**
     * Process all voices and return mono mixed output (legacy)
     */
    float Process()
    {
        float out    = 0.0f;
        active_count_ = 0;

        for(uint8_t i = 0; i < NUM_VOICES; i++)
        {
            out += voices_[i].Process();
            if(voices_[i].playing)
            {
                active_count_++;
            }
        }

        return out * master_level_;
    }

    /**
     * Process all voices with stereo panning
     * @param out_left  Pointer to left channel output
     * @param out_right Pointer to right channel output
     */
    void ProcessStereo(float* out_left, float* out_right)
    {
        float left  = 0.0f;
        float right = 0.0f;
        active_count_ = 0;

        for(uint8_t i = 0; i < NUM_VOICES; i++)
        {
            float mono = voices_[i].Process();

            if(voices_[i].playing)
            {
                active_count_++;
            }

            // Apply equal-power panning
            // pan: -1.0 = full left, 0.0 = center, +1.0 = full right
            float pan = voices_[i].pan;

            // Linear panning (simpler, lower CPU)
            float left_gain  = (1.0f - pan) * 0.5f;
            float right_gain = (1.0f + pan) * 0.5f;

            left  += mono * left_gain;
            right += mono * right_gain;
        }

        *out_left  = left * master_level_;
        *out_right = right * master_level_;
    }

    /**
     * Get number of currently active voices
     */
    uint8_t GetActiveCount() const { return active_count_; }

    /**
     * Get voice by index
     */
    DrumVoice& GetVoice(uint8_t idx) { return voices_[idx]; }

    /**
     * Get sample info by index
     */
    const Sample& GetSample(uint8_t idx) const { return samples_[idx]; }

    /**
     * Set per-voice level
     */
    void SetLevel(uint8_t voice, float level)
    {
        if(voice < NUM_VOICES)
        {
            voices_[voice].level = level;
        }
    }

    /**
     * Set per-voice decay rate
     */
    void SetDecay(uint8_t voice, float decay)
    {
        if(voice < NUM_VOICES)
        {
            voices_[voice].decay = decay;
        }
    }

    /**
     * Set per-voice pitch
     */
    void SetPitch(uint8_t voice, float pitch)
    {
        if(voice < NUM_VOICES)
        {
            voices_[voice].pitch = pitch;
        }
    }

    /**
     * Set per-voice pan (-1.0 to +1.0)
     */
    void SetPan(uint8_t voice, float pan)
    {
        if(voice < NUM_VOICES)
        {
            // Clamp to valid range
            if(pan < -1.0f) pan = -1.0f;
            if(pan > 1.0f) pan = 1.0f;
            voices_[voice].pan = pan;
        }
    }

    /**
     * Get per-voice pan
     */
    float GetPan(uint8_t voice) const
    {
        if(voice < NUM_VOICES)
        {
            return voices_[voice].pan;
        }
        return 0.0f;
    }

    /**
     * Get per-voice level
     */
    float GetLevel(uint8_t voice) const
    {
        if(voice < NUM_VOICES)
        {
            return voices_[voice].level;
        }
        return 1.0f;
    }

    /**
     * Set master level (scales all drums together)
     */
    void SetMasterLevel(float level)
    {
        if(level < 0.0f) level = 0.0f;
        if(level > 1.0f) level = 1.0f;
        master_level_ = level;
    }

    /**
     * Get master level
     */
    float GetMasterLevel() const { return master_level_; }

  private:
    DrumVoice        voices_[NUM_VOICES];
    Sample           samples_[NUM_VOICES];
    volatile uint8_t active_count_;
    float            master_level_;
};

} // namespace Sampler

#endif // GROOVYDAISY_SAMPLER_H
