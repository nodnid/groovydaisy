#pragma once
#ifndef GROOVYDAISY_SYNTH_H
#define GROOVYDAISY_SYNTH_H

#include <stdint.h>
#include <cmath>
#include <cstdio>
#include "daisysp.h"

/**
 * GroovyDaisy 6-Voice Polyphonic Synthesizer
 *
 * Subtractive synthesis with:
 * - 2 oscillators per voice (with waveform selection and detune)
 * - State variable filter (lowpass) with envelope
 * - ADSR envelopes for amplitude and filter
 * - Velocity sensitivity for amp and filter
 * - Voice stealing (oldest note)
 * - Factory presets and parameter control via CC/companion
 */

namespace Synth
{

using namespace daisysp;

// Constants
constexpr uint8_t NUM_VOICES = 6;  // Back to 6 with filter update optimization
constexpr uint8_t SYNTH_CHANNEL = 0;  // Channel 1 (0-indexed)
constexpr uint8_t NUM_FACTORY_PRESETS = 4;
constexpr uint16_t FILTER_UPDATE_RATE = 64;  // Update filter params every N samples (~750Hz)

// Waveform types (matches DaisySP Oscillator waveforms)
enum Waveform : uint8_t
{
    WAVE_SIN = 0,
    WAVE_TRI = 1,
    WAVE_SAW = 2,  // Polyblep
    WAVE_SQUARE = 3,  // Polyblep
    WAVE_COUNT
};

// Parameter IDs for SetParam()
enum ParamId : uint8_t
{
    PARAM_OSC1_WAVE = 0,
    PARAM_OSC2_WAVE,
    PARAM_OSC1_LEVEL,
    PARAM_OSC2_LEVEL,
    PARAM_OSC2_DETUNE,
    PARAM_FILTER_CUTOFF,
    PARAM_FILTER_RES,
    PARAM_FILTER_ENV_AMT,
    PARAM_AMP_ATTACK,
    PARAM_AMP_DECAY,
    PARAM_AMP_SUSTAIN,
    PARAM_AMP_RELEASE,
    PARAM_FILT_ATTACK,
    PARAM_FILT_DECAY,
    PARAM_FILT_SUSTAIN,
    PARAM_FILT_RELEASE,
    PARAM_VEL_TO_AMP,
    PARAM_VEL_TO_FILTER,
    PARAM_LEVEL,
    PARAM_PAN,
    PARAM_MASTER_LEVEL,
    PARAM_COUNT
};

/**
 * All controllable synth parameters
 */
struct SynthParams
{
    // Oscillators
    uint8_t osc1_wave;      // 0-3: sin, tri, saw, square
    uint8_t osc2_wave;
    float osc1_level;       // 0.0-1.0
    float osc2_level;
    int8_t osc2_detune;     // -24 to +24 semitones

    // Filter
    float filter_cutoff;    // 20-20000 Hz
    float filter_res;       // 0.0-1.0
    float filter_env_amt;   // 0.0-1.0 (scaled to frequency internally)

    // Amp Envelope (times in seconds)
    float amp_attack;       // 0.001-5.0
    float amp_decay;
    float amp_sustain;      // 0.0-1.0
    float amp_release;

    // Filter Envelope
    float filt_attack;
    float filt_decay;
    float filt_sustain;     // 0.0-1.0
    float filt_release;

    // Velocity sensitivity
    float vel_to_amp;       // 0.0-1.0
    float vel_to_filter;    // 0.0-1.0

    // Output
    float level;            // 0.0-1.0 (synth level)
    float pan;              // -1.0 to +1.0 (stereo position)
    float master_level;     // 0.0-1.0 (overall synth master)

    /**
     * Initialize with default "Init Patch" values
     */
    void InitPatch()
    {
        osc1_wave = WAVE_SAW;
        osc2_wave = WAVE_SQUARE;
        osc1_level = 1.0f;
        osc2_level = 0.5f;
        osc2_detune = 0;

        filter_cutoff = 2000.0f;
        filter_res = 0.3f;
        filter_env_amt = 0.5f;

        amp_attack = 0.01f;
        amp_decay = 0.2f;
        amp_sustain = 0.7f;
        amp_release = 0.3f;

        filt_attack = 0.01f;
        filt_decay = 0.3f;
        filt_sustain = 0.3f;
        filt_release = 0.3f;

        vel_to_amp = 0.5f;
        vel_to_filter = 0.3f;

        level = 0.7f;
        pan = 0.0f;           // Center
        master_level = 1.0f;  // Full
    }
};

/**
 * Factory presets
 */
struct FactoryPresets
{
    static void GetPreset(uint8_t index, SynthParams& params)
    {
        switch(index)
        {
            case 0:  // Init Patch
                params.InitPatch();
                break;

            case 1:  // Warm Pad
                params.osc1_wave = WAVE_SAW;
                params.osc2_wave = WAVE_SAW;
                params.osc1_level = 0.7f;
                params.osc2_level = 0.7f;
                params.osc2_detune = 7;  // Detune up a fifth

                params.filter_cutoff = 800.0f;
                params.filter_res = 0.2f;
                params.filter_env_amt = 0.3f;

                params.amp_attack = 0.4f;
                params.amp_decay = 0.5f;
                params.amp_sustain = 0.8f;
                params.amp_release = 0.8f;

                params.filt_attack = 0.5f;
                params.filt_decay = 1.0f;
                params.filt_sustain = 0.4f;
                params.filt_release = 0.8f;

                params.vel_to_amp = 0.3f;
                params.vel_to_filter = 0.2f;
                params.level = 0.6f;
                break;

            case 2:  // Pluck Lead
                params.osc1_wave = WAVE_SAW;
                params.osc2_wave = WAVE_SQUARE;
                params.osc1_level = 1.0f;
                params.osc2_level = 0.3f;
                params.osc2_detune = 0;

                params.filter_cutoff = 3000.0f;
                params.filter_res = 0.6f;
                params.filter_env_amt = 0.7f;

                params.amp_attack = 0.001f;
                params.amp_decay = 0.15f;
                params.amp_sustain = 0.3f;
                params.amp_release = 0.2f;

                params.filt_attack = 0.001f;
                params.filt_decay = 0.2f;
                params.filt_sustain = 0.2f;
                params.filt_release = 0.15f;

                params.vel_to_amp = 0.8f;
                params.vel_to_filter = 0.6f;
                params.level = 0.7f;
                break;

            case 3:  // Bass
                params.osc1_wave = WAVE_SAW;
                params.osc2_wave = WAVE_SQUARE;
                params.osc1_level = 1.0f;
                params.osc2_level = 0.6f;
                params.osc2_detune = -12;  // Octave down

                params.filter_cutoff = 500.0f;
                params.filter_res = 0.4f;
                params.filter_env_amt = 0.6f;

                params.amp_attack = 0.005f;
                params.amp_decay = 0.3f;
                params.amp_sustain = 0.6f;
                params.amp_release = 0.15f;

                params.filt_attack = 0.001f;
                params.filt_decay = 0.25f;
                params.filt_sustain = 0.2f;
                params.filt_release = 0.1f;

                params.vel_to_amp = 0.7f;
                params.vel_to_filter = 0.5f;
                params.level = 0.8f;
                break;

            default:
                params.InitPatch();
                break;
        }
    }

    static const char* GetPresetName(uint8_t index)
    {
        switch(index)
        {
            case 0: return "Init Patch";
            case 1: return "Warm Pad";
            case 2: return "Pluck Lead";
            case 3: return "Bass";
            default: return "Unknown";
        }
    }
};

/**
 * Single synth voice with oscillators, filter, and envelopes
 */
struct SynthVoice
{
    Oscillator osc1;
    Oscillator osc2;
    Svf filter;
    Adsr amp_env;
    Adsr filt_env;

    uint8_t note;           // MIDI note number
    uint8_t velocity;       // Trigger velocity (0-127)
    bool active;            // Voice is sounding
    bool gate;              // Key is held down
    uint32_t start_time;    // For voice stealing
    uint32_t release_samples;  // Samples since gate released (for stuck detection)
    float last_env;         // Last envelope value (for diagnostics)
    float cached_filt_env;  // Cached filter envelope for reduced update rate

    float sample_rate_ = 48000.0f;  // Store for Reset(), default to safe value

    void Init(float sample_rate)
    {
        sample_rate_ = sample_rate;
        osc1.Init(sample_rate);
        osc2.Init(sample_rate);
        filter.Init(sample_rate);
        amp_env.Init(sample_rate);
        filt_env.Init(sample_rate);

        note = 0;
        velocity = 0;
        active = false;
        gate = false;
        start_time = 0;
        release_samples = 0;
        last_env = 0.0f;
        cached_filt_env = 0.0f;
    }

    // Reset filter state to prevent accumulated errors/noise
    void ResetFilter()
    {
        filter.Init(sample_rate_);
    }

    void SetWaveform(Oscillator& osc, uint8_t wave)
    {
        switch(wave)
        {
            case WAVE_SIN:
                osc.SetWaveform(Oscillator::WAVE_SIN);
                break;
            case WAVE_TRI:
                osc.SetWaveform(Oscillator::WAVE_POLYBLEP_TRI);
                break;
            case WAVE_SAW:
                osc.SetWaveform(Oscillator::WAVE_POLYBLEP_SAW);
                break;
            case WAVE_SQUARE:
                osc.SetWaveform(Oscillator::WAVE_POLYBLEP_SQUARE);
                break;
            default:
                osc.SetWaveform(Oscillator::WAVE_POLYBLEP_SAW);
                break;
        }
    }
};

/**
 * Main 6-voice polyphonic synth engine
 */
class Engine
{
  public:
    /**
     * Initialize the synth engine
     */
    void Init(float sample_rate)
    {
        sample_rate_ = sample_rate;

        for(uint8_t i = 0; i < NUM_VOICES; i++)
        {
            voices_[i].Init(sample_rate);
        }

        // Load default preset
        FactoryPresets::GetPreset(0, params_);
        ApplyParams();

        active_count_ = 0;
        time_counter_ = 0;
        current_preset_ = 0;
        filter_update_counter_ = 0;
        nan_detected_ = false;
        stuck_voice_detected_ = false;
    }

    /**
     * Trigger a note on
     */
    void NoteOn(uint8_t note, uint8_t velocity)
    {
        // Find a voice to use
        int voice_idx = FindFreeVoice();
        SynthVoice& v = voices_[voice_idx];

        // If stealing an active voice, release it first and clear state
        if(v.active && v.gate)
        {
            v.gate = false;
            v.ResetFilter();  // Clear filter state to prevent carried-over artifacts
        }

        // Set up the voice
        v.note = note;
        v.velocity = velocity;
        v.active = true;
        v.gate = true;
        v.start_time = time_counter_++;

        // Reset oscillator phase to prevent clicks from random phase position
        v.osc1.Reset();
        v.osc2.Reset();

        // Set oscillator frequencies
        float freq = mtof(note);
        v.osc1.SetFreq(freq);

        // Osc2 with detune (semitones)
        float detune_ratio = powf(2.0f, params_.osc2_detune / 12.0f);
        v.osc2.SetFreq(freq * detune_ratio);

        // Set oscillator waveforms
        v.SetWaveform(v.osc1, params_.osc1_wave);
        v.SetWaveform(v.osc2, params_.osc2_wave);

        // Set oscillator amplitudes (normalized to prevent clipping before filter)
        float osc_sum = params_.osc1_level + params_.osc2_level;
        float osc_scale = (osc_sum > 1.0f) ? (1.0f / osc_sum) : 1.0f;
        v.osc1.SetAmp(params_.osc1_level * osc_scale);
        v.osc2.SetAmp(params_.osc2_level * osc_scale);

        // Hard retrigger envelopes (reset to zero for clean attack)
        v.amp_env.Retrigger(true);
        v.filt_env.Retrigger(true);
    }

    /**
     * Release a note
     */
    void NoteOff(uint8_t note)
    {
        // Release ALL voices playing this note (not just the first one)
        // This handles the case where the same note was triggered multiple times
        for(uint8_t i = 0; i < NUM_VOICES; i++)
        {
            if(voices_[i].active && voices_[i].note == note && voices_[i].gate)
            {
                voices_[i].gate = false;
                // Note: voice stays active until amp envelope finishes
            }
        }
    }

    /**
     * Release all notes (for transport stop, panic, etc.)
     */
    void AllNotesOff()
    {
        for(uint8_t i = 0; i < NUM_VOICES; i++)
        {
            voices_[i].gate = false;
        }
    }

    /**
     * Handle MIDI input on synth channel
     * Returns true if the event was handled
     */
    bool HandleMidi(uint8_t channel, uint8_t status_type, uint8_t data1, uint8_t data2)
    {
        if(channel != SYNTH_CHANNEL)
            return false;

        uint8_t msg_type = status_type & 0xF0;

        switch(msg_type)
        {
            case 0x90:  // Note On
                if(data2 > 0)
                {
                    NoteOn(data1, data2);
                    return true;
                }
                // Fall through: velocity 0 = note off
            case 0x80:  // Note Off
                NoteOff(data1);
                return true;
            default:
                return false;
        }
    }

    /**
     * Soft clip function to prevent harsh distortion
     */
    float SoftClip(float x)
    {
        // Soft saturation using tanh-like curve
        if(x > 1.0f)
            return 1.0f - 1.0f / (1.0f + x);
        else if(x < -1.0f)
            return -1.0f + 1.0f / (1.0f - x);
        return x;
    }

    /**
     * Process all voices and return mono output
     */
    float Process()
    {
        float out = 0.0f;
        active_count_ = 0;

        // Determine if we should update filter params this sample
        bool update_filters = (filter_update_counter_ == 0);
        filter_update_counter_++;
        if(filter_update_counter_ >= FILTER_UPDATE_RATE)
            filter_update_counter_ = 0;

        for(uint8_t i = 0; i < NUM_VOICES; i++)
        {
            SynthVoice& v = voices_[i];

            if(!v.active)
                continue;

            // Process oscillators
            float osc_out = v.osc1.Process() + v.osc2.Process();

            // Process filter envelope every sample (for smooth modulation)
            float filt_env = v.filt_env.Process(v.gate);

            // Velocity normalization (used for both filter and amp modulation)
            float vel_norm = v.velocity / 127.0f;

            // Only update filter parameters every FILTER_UPDATE_RATE samples (~750Hz)
            // This is the expensive part - SetFreq/SetRes recalculate coefficients
            if(update_filters)
            {
                v.cached_filt_env = filt_env;

                // Calculate filter cutoff with envelope and velocity modulation
                float vel_mod = (vel_norm - 0.5f) * params_.vel_to_filter * 1500.0f;
                float env_mod = v.cached_filt_env * params_.filter_env_amt * 2000.0f;
                float cutoff = params_.filter_cutoff + vel_mod + env_mod;
                cutoff = fclamp(cutoff, 20.0f, 12000.0f);

                v.filter.SetFreq(cutoff);
                v.filter.SetRes(fminf(params_.filter_res, 0.7f));
            }

            // Filter.Process() is cheap - called every sample
            v.filter.Process(osc_out);
            float filt_out = v.filter.Low();

            // Process amplitude envelope
            float amp_env = v.amp_env.Process(v.gate);
            v.last_env = amp_env;

            // Apply velocity to amplitude
            float vel_amp = 1.0f - params_.vel_to_amp + (vel_norm * params_.vel_to_amp);

            // Track release time for stuck detection
            if(!v.gate)
            {
                v.release_samples++;
                if(v.release_samples > static_cast<uint32_t>(sample_rate_ * 3.0f))
                {
                    v.active = false;
                    v.ResetFilter();
                    v.release_samples = 0;
                    stuck_voice_detected_ = true;
                    continue;
                }
            }
            else
            {
                v.release_samples = 0;
            }

            // Check if voice has finished
            if(!v.gate && amp_env < 0.01f)
            {
                v.active = false;
                v.ResetFilter();
                v.release_samples = 0;
                continue;
            }

            // Check for NaN/Inf
            if(std::isnan(filt_out) || std::isinf(filt_out))
            {
                v.ResetFilter();
                v.active = false;
                v.release_samples = 0;
                nan_detected_ = true;
                continue;
            }

            // Mix voice output (0.15 per voice = 0.60 max with 4 voices)
            out += filt_out * amp_env * vel_amp * 0.15f;
            active_count_++;
        }

        // Apply level and soft clip (master_level applied in stereo output)
        return SoftClip(out * params_.level);
    }

    /**
     * Process all voices and return stereo output with panning
     */
    void ProcessStereo(float* out_left, float* out_right)
    {
        float mono = Process();  // Get mono sum

        // Apply pan and master level
        // pan: -1.0 = full left, 0.0 = center, +1.0 = full right
        float left_gain  = (1.0f - params_.pan) * 0.5f;
        float right_gain = (1.0f + params_.pan) * 0.5f;

        *out_left  = mono * left_gain * params_.master_level;
        *out_right = mono * right_gain * params_.master_level;
    }

    /**
     * Get number of active voices
     */
    uint8_t GetActiveCount() const { return active_count_; }

    /**
     * Check if NaN was detected (clears flag)
     */
    bool HadNaN()
    {
        bool v = nan_detected_;
        nan_detected_ = false;
        return v;
    }

    /**
     * Check if stuck voice was detected (clears flag)
     */
    bool HadStuckVoice()
    {
        bool v = stuck_voice_detected_;
        stuck_voice_detected_ = false;
        return v;
    }

    /**
     * Get diagnostic string for all voices
     * Note: Using integer for envelope (0-99) because nano libc doesn't support %f
     */
    void GetVoiceDiagString(char* buf, size_t len)
    {
        int pos = 0;
        pos += snprintf(buf + pos, len - pos, "V:");
        for(int i = 0; i < NUM_VOICES && pos < (int)len - 20; i++)
        {
            SynthVoice& v = voices_[i];
            int env_pct = static_cast<int>(v.last_env * 99.0f);  // 0-99%
            pos += snprintf(buf + pos, len - pos, " %d[%c%c n%d e%d]",
                i,
                v.active ? 'A' : '-',
                v.gate ? 'G' : '-',
                v.note,
                env_pct);
        }
    }

    /**
     * Get current parameters
     */
    const SynthParams& GetParams() const { return params_; }

    /**
     * Set a single parameter by ID
     */
    void SetParam(ParamId id, float value)
    {
        switch(id)
        {
            case PARAM_OSC1_WAVE:
                params_.osc1_wave = static_cast<uint8_t>(value) % WAVE_COUNT;
                break;
            case PARAM_OSC2_WAVE:
                params_.osc2_wave = static_cast<uint8_t>(value) % WAVE_COUNT;
                break;
            case PARAM_OSC1_LEVEL:
                params_.osc1_level = fclamp(value, 0.0f, 1.0f);
                break;
            case PARAM_OSC2_LEVEL:
                params_.osc2_level = fclamp(value, 0.0f, 1.0f);
                break;
            case PARAM_OSC2_DETUNE:
                params_.osc2_detune = static_cast<int8_t>(fclamp(value, -24.0f, 24.0f));
                break;
            case PARAM_FILTER_CUTOFF:
                params_.filter_cutoff = fclamp(value, 20.0f, 20000.0f);
                break;
            case PARAM_FILTER_RES:
                params_.filter_res = fclamp(value, 0.0f, 1.0f);
                break;
            case PARAM_FILTER_ENV_AMT:
                params_.filter_env_amt = fclamp(value, 0.0f, 1.0f);
                break;
            case PARAM_AMP_ATTACK:
                params_.amp_attack = fclamp(value, 0.001f, 5.0f);
                ApplyEnvelopes();
                break;
            case PARAM_AMP_DECAY:
                params_.amp_decay = fclamp(value, 0.001f, 5.0f);
                ApplyEnvelopes();
                break;
            case PARAM_AMP_SUSTAIN:
                params_.amp_sustain = fclamp(value, 0.0f, 1.0f);
                ApplyEnvelopes();
                break;
            case PARAM_AMP_RELEASE:
                params_.amp_release = fclamp(value, 0.001f, 5.0f);
                ApplyEnvelopes();
                break;
            case PARAM_FILT_ATTACK:
                params_.filt_attack = fclamp(value, 0.001f, 5.0f);
                ApplyEnvelopes();
                break;
            case PARAM_FILT_DECAY:
                params_.filt_decay = fclamp(value, 0.001f, 5.0f);
                ApplyEnvelopes();
                break;
            case PARAM_FILT_SUSTAIN:
                params_.filt_sustain = fclamp(value, 0.0f, 1.0f);
                ApplyEnvelopes();
                break;
            case PARAM_FILT_RELEASE:
                params_.filt_release = fclamp(value, 0.001f, 5.0f);
                ApplyEnvelopes();
                break;
            case PARAM_VEL_TO_AMP:
                params_.vel_to_amp = fclamp(value, 0.0f, 1.0f);
                break;
            case PARAM_VEL_TO_FILTER:
                params_.vel_to_filter = fclamp(value, 0.0f, 1.0f);
                break;
            case PARAM_LEVEL:
                params_.level = fclamp(value, 0.0f, 1.0f);
                break;
            case PARAM_PAN:
                params_.pan = fclamp(value, -1.0f, 1.0f);
                break;
            case PARAM_MASTER_LEVEL:
                params_.master_level = fclamp(value, 0.0f, 1.0f);
                break;
            default:
                break;
        }
    }

    /**
     * Load a factory preset
     */
    void LoadPreset(uint8_t index)
    {
        if(index >= NUM_FACTORY_PRESETS)
            return;

        FactoryPresets::GetPreset(index, params_);
        ApplyParams();
        current_preset_ = index;
    }

    /**
     * Set full preset from companion
     */
    void SetPreset(const SynthParams& p)
    {
        params_ = p;
        ApplyParams();
    }

    /**
     * Get current preset index
     */
    uint8_t GetCurrentPreset() const { return current_preset_; }

  private:
    /**
     * Find a free voice or steal the oldest
     */
    int FindFreeVoice()
    {
        // First, look for an inactive voice
        for(int i = 0; i < NUM_VOICES; i++)
        {
            if(!voices_[i].active)
                return i;
        }

        // All voices active - steal the oldest
        int oldest = 0;
        uint32_t oldest_time = voices_[0].start_time;

        for(int i = 1; i < NUM_VOICES; i++)
        {
            if(voices_[i].start_time < oldest_time)
            {
                oldest = i;
                oldest_time = voices_[i].start_time;
            }
        }

        return oldest;
    }

    /**
     * Apply current parameters to all voices
     */
    void ApplyParams()
    {
        ApplyEnvelopes();
    }

    /**
     * Apply envelope parameters to all voices
     */
    void ApplyEnvelopes()
    {
        for(uint8_t i = 0; i < NUM_VOICES; i++)
        {
            SynthVoice& v = voices_[i];

            // Amp envelope
            v.amp_env.SetTime(ADSR_SEG_ATTACK, params_.amp_attack);
            v.amp_env.SetTime(ADSR_SEG_DECAY, params_.amp_decay);
            v.amp_env.SetSustainLevel(params_.amp_sustain);
            v.amp_env.SetTime(ADSR_SEG_RELEASE, params_.amp_release);

            // Filter envelope
            v.filt_env.SetTime(ADSR_SEG_ATTACK, params_.filt_attack);
            v.filt_env.SetTime(ADSR_SEG_DECAY, params_.filt_decay);
            v.filt_env.SetSustainLevel(params_.filt_sustain);
            v.filt_env.SetTime(ADSR_SEG_RELEASE, params_.filt_release);
        }
    }

    SynthVoice voices_[NUM_VOICES];
    SynthParams params_;
    float sample_rate_;
    volatile uint8_t active_count_;
    uint32_t time_counter_;
    uint8_t current_preset_;
    uint16_t filter_update_counter_;  // Counter for reduced filter update rate

    // Diagnostic flags (set in audio callback, read in main loop)
    volatile bool nan_detected_;
    volatile bool stuck_voice_detected_;
};

} // namespace Synth

#endif // GROOVYDAISY_SYNTH_H
