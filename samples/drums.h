#pragma once
#ifndef GROOVYDAISY_SAMPLES_DRUMS_H
#define GROOVYDAISY_SAMPLES_DRUMS_H

/**
 * GroovyDaisy Synthesized Drum Samples
 *
 * Simple synthesized drum sounds for testing.
 * These can be replaced with real samples later.
 *
 * All samples are mono, 48kHz, normalized to -1.0 to 1.0
 */

#include <stddef.h>
#include <cmath>

// Sample rate for calculations
constexpr float SAMPLE_RATE = 48000.0f;

// Pi constant - use different name to avoid conflict with DaisySP
constexpr float DRUM_PI = 3.14159265358979323846f;

/**
 * Generate samples at compile time is not practical in C++,
 * so we generate them at runtime into SDRAM.
 *
 * Sample lengths (in samples at 48kHz):
 * - Kick: ~0.3 sec = 14400 samples
 * - Snare: ~0.25 sec = 12000 samples
 * - Closed HH: ~0.1 sec = 4800 samples
 * - Open HH: ~0.4 sec = 19200 samples
 * - Clap: ~0.3 sec = 14400 samples
 * - Tom Low: ~0.4 sec = 19200 samples
 * - Tom Mid: ~0.3 sec = 14400 samples
 * - Rim: ~0.1 sec = 4800 samples
 */

namespace DrumSamples
{

// Sample lengths
constexpr size_t KICK_LENGTH     = 14400;
constexpr size_t SNARE_LENGTH    = 12000;
constexpr size_t HIHAT_C_LENGTH  = 4800;
constexpr size_t HIHAT_O_LENGTH  = 19200;
constexpr size_t CLAP_LENGTH     = 14400;
constexpr size_t TOM_LOW_LENGTH  = 19200;
constexpr size_t TOM_MID_LENGTH  = 14400;
constexpr size_t RIM_LENGTH      = 4800;

// Total samples needed (sum of all)
constexpr size_t TOTAL_SAMPLES = KICK_LENGTH + SNARE_LENGTH + HIHAT_C_LENGTH +
                                  HIHAT_O_LENGTH + CLAP_LENGTH + TOM_LOW_LENGTH +
                                  TOM_MID_LENGTH + RIM_LENGTH;

/**
 * Simple pseudo-random number generator for noise
 * (consistent across runs for reproducible samples)
 */
inline float Noise(uint32_t& seed)
{
    seed = seed * 1664525u + 1013904223u;
    return (static_cast<float>(seed) / static_cast<float>(0xFFFFFFFFu)) * 2.0f - 1.0f;
}

/**
 * Generate kick drum sample
 * Low sine wave with pitch drop and amplitude decay
 */
inline void GenerateKick(float* buffer, size_t length)
{
    float phase = 0.0f;

    for(size_t i = 0; i < length; i++)
    {
        float t = static_cast<float>(i) / SAMPLE_RATE;

        // Frequency sweep: 150Hz down to 50Hz
        float freq = 50.0f + 100.0f * expf(-t * 20.0f);

        // Amplitude envelope: fast attack, medium decay
        float amp = expf(-t * 8.0f);

        // Add a bit of punch at the start
        if(t < 0.005f)
        {
            amp = 1.0f;
        }

        // Generate sine
        phase += (2.0f * DRUM_PI * freq) / SAMPLE_RATE;
        if(phase > 2.0f * DRUM_PI)
            phase -= 2.0f * DRUM_PI;

        buffer[i] = sinf(phase) * amp * 0.9f;
    }
}

/**
 * Generate snare drum sample
 * Pitched body (sine) + noise (snares)
 */
inline void GenerateSnare(float* buffer, size_t length)
{
    uint32_t seed = 12345;
    float    phase = 0.0f;

    for(size_t i = 0; i < length; i++)
    {
        float t = static_cast<float>(i) / SAMPLE_RATE;

        // Body: 200Hz sine with fast decay
        float body_freq = 180.0f;
        float body_amp  = expf(-t * 25.0f);

        phase += (2.0f * DRUM_PI * body_freq) / SAMPLE_RATE;
        if(phase > 2.0f * DRUM_PI)
            phase -= 2.0f * DRUM_PI;

        float body = sinf(phase) * body_amp;

        // Snares: filtered noise with medium decay
        float noise_amp = expf(-t * 15.0f);
        float noise_val = Noise(seed) * noise_amp;

        // Mix body and snares
        buffer[i] = (body * 0.5f + noise_val * 0.6f) * 0.8f;
    }
}

/**
 * Generate closed hi-hat sample
 * High frequency noise with very short decay
 */
inline void GenerateHihatClosed(float* buffer, size_t length)
{
    uint32_t seed = 67890;

    for(size_t i = 0; i < length; i++)
    {
        float t = static_cast<float>(i) / SAMPLE_RATE;

        // Very short envelope
        float amp = expf(-t * 50.0f);

        buffer[i] = Noise(seed) * amp * 0.5f;
    }
}

/**
 * Generate open hi-hat sample
 * High frequency noise with longer decay
 */
inline void GenerateHihatOpen(float* buffer, size_t length)
{
    uint32_t seed = 11111;

    for(size_t i = 0; i < length; i++)
    {
        float t = static_cast<float>(i) / SAMPLE_RATE;

        // Longer envelope
        float amp = expf(-t * 8.0f);

        buffer[i] = Noise(seed) * amp * 0.5f;
    }
}

/**
 * Generate clap sample
 * Multiple noise bursts with smoother envelope
 */
inline void GenerateClap(float* buffer, size_t length)
{
    uint32_t seed = 22222;

    for(size_t i = 0; i < length; i++)
    {
        float t = static_cast<float>(i) / SAMPLE_RATE;

        // Create multiple short bursts using overlapping envelopes
        float amp = 0.0f;

        // 3-4 quick bursts in first 25ms, then decay tail
        // Each burst is a quick attack/decay
        float burst1 = expf(-fabsf(t - 0.003f) * 800.0f);  // 3ms
        float burst2 = expf(-fabsf(t - 0.010f) * 600.0f);  // 10ms
        float burst3 = expf(-fabsf(t - 0.018f) * 500.0f);  // 18ms
        float tail   = expf(-(t - 0.020f) * 15.0f);        // main decay

        // Combine bursts (max of bursts in early part, then tail)
        if(t < 0.025f)
        {
            amp = fmaxf(fmaxf(burst1, burst2), burst3) * 0.7f;
        }
        else
        {
            amp = tail * 0.8f;
        }

        buffer[i] = Noise(seed) * amp * 0.5f;
    }
}

/**
 * Generate low tom sample
 */
inline void GenerateTomLow(float* buffer, size_t length)
{
    float phase = 0.0f;

    for(size_t i = 0; i < length; i++)
    {
        float t = static_cast<float>(i) / SAMPLE_RATE;

        // Frequency with slight pitch drop
        float freq = 80.0f + 40.0f * expf(-t * 10.0f);

        // Envelope
        float amp = expf(-t * 6.0f);

        phase += (2.0f * DRUM_PI * freq) / SAMPLE_RATE;
        if(phase > 2.0f * DRUM_PI)
            phase -= 2.0f * DRUM_PI;

        buffer[i] = sinf(phase) * amp * 0.85f;
    }
}

/**
 * Generate mid tom sample
 */
inline void GenerateTomMid(float* buffer, size_t length)
{
    float phase = 0.0f;

    for(size_t i = 0; i < length; i++)
    {
        float t = static_cast<float>(i) / SAMPLE_RATE;

        // Frequency with slight pitch drop
        float freq = 120.0f + 50.0f * expf(-t * 12.0f);

        // Envelope
        float amp = expf(-t * 8.0f);

        phase += (2.0f * DRUM_PI * freq) / SAMPLE_RATE;
        if(phase > 2.0f * DRUM_PI)
            phase -= 2.0f * DRUM_PI;

        buffer[i] = sinf(phase) * amp * 0.8f;
    }
}

/**
 * Generate rim shot sample
 * Short high-pitched click
 */
inline void GenerateRim(float* buffer, size_t length)
{
    uint32_t seed  = 33333;
    float    phase = 0.0f;

    for(size_t i = 0; i < length; i++)
    {
        float t = static_cast<float>(i) / SAMPLE_RATE;

        // Very short envelope
        float amp = expf(-t * 80.0f);

        // Mix of high sine and noise for click
        phase += (2.0f * DRUM_PI * 800.0f) / SAMPLE_RATE;
        if(phase > 2.0f * DRUM_PI)
            phase -= 2.0f * DRUM_PI;

        float tone  = sinf(phase) * amp;
        float noise = Noise(seed) * amp * 0.5f;

        buffer[i] = (tone + noise) * 0.7f;
    }
}

/**
 * Sample buffer structure that holds all samples in SDRAM
 */
struct SampleBank
{
    float kick[KICK_LENGTH];
    float snare[SNARE_LENGTH];
    float hihat_closed[HIHAT_C_LENGTH];
    float hihat_open[HIHAT_O_LENGTH];
    float clap[CLAP_LENGTH];
    float tom_low[TOM_LOW_LENGTH];
    float tom_mid[TOM_MID_LENGTH];
    float rim[RIM_LENGTH];

    /**
     * Generate all samples
     * Call this once after Init (in main, not in audio callback!)
     */
    void Generate()
    {
        GenerateKick(kick, KICK_LENGTH);
        GenerateSnare(snare, SNARE_LENGTH);
        GenerateHihatClosed(hihat_closed, HIHAT_C_LENGTH);
        GenerateHihatOpen(hihat_open, HIHAT_O_LENGTH);
        GenerateClap(clap, CLAP_LENGTH);
        GenerateTomLow(tom_low, TOM_LOW_LENGTH);
        GenerateTomMid(tom_mid, TOM_MID_LENGTH);
        GenerateRim(rim, RIM_LENGTH);
    }
};

} // namespace DrumSamples

#endif // GROOVYDAISY_SAMPLES_DRUMS_H
