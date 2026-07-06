#pragma once
#ifndef GROOVYDAISY_CC_MAP_H
#define GROOVYDAISY_CC_MAP_H

#include <stdint.h>
#include <cmath>
#include "synth.h"

/**
 * GroovyDaisy CC Mapping with 4-Bank System
 *
 * Maps MIDI CC messages to parameters based on current bank.
 * Bank switching via CC 1 (Next) and CC 2 (Prev).
 * Fader pickup mode prevents value jumps on bank switch.
 *
 * Hardware: Arturia KeyLab Essential 61
 * - Encoders (L->R): CC 74, 71, 76, 77, 93, 18, 19, 16, 17
 * - Faders (L->R):   CC 73, 75, 79, 72, 80, 81, 82, 83, 85
 */

namespace CCMap
{

// Bank definitions
enum Bank : uint8_t
{
    BANK_GENERAL = 0,   // Master controls
    BANK_MIX = 1,       // Individual levels + pan
    BANK_SYNTH = 2,     // Sound design
    BANK_SAMPLER = 3,   // Per-drum parameters
    NUM_BANKS = 4
};

// Bank switch CCs
constexpr uint8_t CC_BANK_NEXT = 1;   // Part 1 / Next button
constexpr uint8_t CC_BANK_PREV = 2;   // Part 2 / Prev button

// Hardware CC numbers
constexpr uint8_t NUM_ENCODERS = 9;
constexpr uint8_t NUM_FADERS = 9;

// CC number constants (for automation, router compatibility)
constexpr uint8_t FILTER_CUTOFF = 74;  // Encoder 1
constexpr uint8_t FILTER_RES    = 71;  // Encoder 2
constexpr uint8_t OSC1_WAVE     = 76;  // Encoder 3
constexpr uint8_t OSC2_WAVE     = 77;  // Encoder 4
constexpr uint8_t AMP_ATTACK    = 93;  // Encoder 5
constexpr uint8_t AMP_DECAY     = 18;  // Encoder 6
constexpr uint8_t AMP_SUSTAIN   = 19;  // Encoder 7
constexpr uint8_t AMP_RELEASE   = 16;  // Encoder 8
constexpr uint8_t PATTERN_SEL   = 17;  // Encoder 9

constexpr uint8_t OSC1_LEVEL    = 73;  // Fader 1
constexpr uint8_t OSC2_LEVEL    = 75;  // Fader 2
constexpr uint8_t FILT_ENV_AMT  = 79;  // Fader 3
constexpr uint8_t LFO_DEPTH     = 72;  // Fader 4
constexpr uint8_t DRUM_1_LEVEL  = 80;  // Fader 5
constexpr uint8_t DRUM_2_LEVEL  = 81;  // Fader 6
constexpr uint8_t DRUM_3_LEVEL  = 82;  // Fader 7
constexpr uint8_t DRUM_4_LEVEL  = 83;  // Fader 8
constexpr uint8_t SYNTH_LEVEL   = 85;  // Fader 9

constexpr uint8_t MOD_WHEEL     = 1;   // Mod wheel (shared with bank next)
constexpr uint8_t SUSTAIN       = 64;  // Sustain pedal

// Encoder CCs (in physical order L->R)
constexpr uint8_t ENCODER_CCS[NUM_ENCODERS] = {74, 71, 76, 77, 93, 18, 19, 16, 17};

// Fader CCs (in physical order L->R)
constexpr uint8_t FADER_CCS[NUM_FADERS] = {73, 75, 79, 72, 80, 81, 82, 83, 85};

// Pickup tolerance (±3 CC values)
constexpr uint8_t PICKUP_TOLERANCE = 3;

// Parameter types for routing
enum ParamTarget : uint8_t
{
    TARGET_NONE = 0,
    // Synth params (match Synth::ParamId)
    TARGET_SYNTH_OSC1_WAVE,
    TARGET_SYNTH_OSC2_WAVE,
    TARGET_SYNTH_OSC1_LEVEL,
    TARGET_SYNTH_OSC2_LEVEL,
    TARGET_SYNTH_OSC2_DETUNE,
    TARGET_SYNTH_FILTER_CUTOFF,
    TARGET_SYNTH_FILTER_RES,
    TARGET_SYNTH_FILTER_ENV_AMT,
    TARGET_SYNTH_AMP_ATTACK,
    TARGET_SYNTH_AMP_DECAY,
    TARGET_SYNTH_AMP_SUSTAIN,
    TARGET_SYNTH_AMP_RELEASE,
    TARGET_SYNTH_FILT_ATTACK,
    TARGET_SYNTH_FILT_DECAY,
    TARGET_SYNTH_FILT_SUSTAIN,
    TARGET_SYNTH_FILT_RELEASE,
    TARGET_SYNTH_VEL_TO_AMP,
    TARGET_SYNTH_VEL_TO_FILTER,
    TARGET_SYNTH_LEVEL,
    TARGET_SYNTH_PAN,
    TARGET_SYNTH_MASTER_LEVEL,
    // Drum params
    TARGET_DRUM_1_LEVEL,
    TARGET_DRUM_2_LEVEL,
    TARGET_DRUM_3_LEVEL,
    TARGET_DRUM_4_LEVEL,
    TARGET_DRUM_5_LEVEL,
    TARGET_DRUM_6_LEVEL,
    TARGET_DRUM_7_LEVEL,
    TARGET_DRUM_8_LEVEL,
    TARGET_DRUM_1_PAN,
    TARGET_DRUM_2_PAN,
    TARGET_DRUM_3_PAN,
    TARGET_DRUM_4_PAN,
    TARGET_DRUM_5_PAN,
    TARGET_DRUM_6_PAN,
    TARGET_DRUM_7_PAN,
    TARGET_DRUM_8_PAN,
    TARGET_DRUM_MASTER_LEVEL,
    // Global params
    TARGET_MASTER_OUTPUT,
    // v2 live-channel params (mixer strips)
    TARGET_GUITAR_LEVEL,
    TARGET_GUITAR_PAN,
    TARGET_METRO_LEVEL,
    // v2 capture-length presets (bars 1/2/4/8 per source)
    TARGET_CAPTURE_LEN_PADS,
    TARGET_CAPTURE_LEN_KEYS,
    // v2 groove (Phase 4): swing percent, live-tweakable from hardware
    TARGET_SWING,
    // v2 send FX (Phase 5)
    TARGET_FX_REV_SIZE,
    TARGET_FX_REV_TONE,
    TARGET_FX_DLY_DIV,
    TARGET_FX_DLY_FB,
    TARGET_GTR_SEND_REV,
    TARGET_GTR_SEND_DLY,
    TARGET_SYNTH_SEND_REV,
    TARGET_DRUMS_SEND_REV,
    // v2 drum sound design (Phase 5): per-voice pitch + decay
    TARGET_DRUM_1_PITCH,
    TARGET_DRUM_2_PITCH,
    TARGET_DRUM_3_PITCH,
    TARGET_DRUM_4_PITCH,
    TARGET_DRUM_5_PITCH,
    TARGET_DRUM_6_PITCH,
    TARGET_DRUM_7_PITCH,
    TARGET_DRUM_8_PITCH,
    TARGET_DRUM_1_DECAY,
    TARGET_DRUM_2_DECAY,
    TARGET_DRUM_3_DECAY,
    TARGET_DRUM_4_DECAY,
    TARGET_DRUM_5_DECAY,
    TARGET_DRUM_6_DECAY,
    TARGET_DRUM_7_DECAY,
    TARGET_DRUM_8_DECAY,
    TARGET_COUNT
};

/**
 * Mapping entry: what a control does in a specific bank
 */
struct ControlMapping
{
    ParamTarget target;
    const char* name;
};

/**
 * Bank mappings - what each encoder/fader does per bank
 */
struct BankMappings
{
    const char* bank_name;
    ControlMapping encoders[NUM_ENCODERS];
    ControlMapping faders[NUM_FADERS];
};

// Bank 0: Live (masters, space, groove — the campfire bank, Phase 5
// "Bank 4 finalized". Vel>Amp/Vel>Flt moved to the app's synth panel.)
constexpr BankMappings BANK_GENERAL_MAP = {
    "Live",
    // Encoders: pan, the space, the groove
    {
        {TARGET_GUITAR_PAN, "GtrPan"},
        {TARGET_FX_REV_SIZE, "RevSize"},
        {TARGET_FX_REV_TONE, "RevTone"},
        {TARGET_FX_DLY_DIV, "DlyDiv"},
        {TARGET_FX_DLY_FB, "DlyFb"},
        {TARGET_SWING, "Swing"},
        {TARGET_CAPTURE_LEN_PADS, "PadLen"},
        {TARGET_CAPTURE_LEN_KEYS, "KeyLen"},
        {TARGET_NONE, "---"},
    },
    // Faders: masters + sends
    {
        {TARGET_DRUM_MASTER_LEVEL, "DrumMst"},
        {TARGET_SYNTH_MASTER_LEVEL, "SynthMst"},
        {TARGET_GUITAR_LEVEL, "Guitar"},
        {TARGET_METRO_LEVEL, "Metro"},
        {TARGET_GTR_SEND_REV, "GtrRev"},
        {TARGET_GTR_SEND_DLY, "GtrDly"},
        {TARGET_SYNTH_SEND_REV, "SynRev"},
        {TARGET_DRUMS_SEND_REV, "DrmRev"},
        {TARGET_MASTER_OUTPUT, "Master"},
    }
};

// Bank 1: Mix (Individual Levels + Pan)
constexpr BankMappings BANK_MIX_MAP = {
    "Mix",
    // Encoders (Pan)
    {
        {TARGET_DRUM_1_PAN, "D1 Pan"},
        {TARGET_DRUM_2_PAN, "D2 Pan"},
        {TARGET_DRUM_3_PAN, "D3 Pan"},
        {TARGET_DRUM_4_PAN, "D4 Pan"},
        {TARGET_DRUM_5_PAN, "D5 Pan"},
        {TARGET_DRUM_6_PAN, "D6 Pan"},
        {TARGET_DRUM_7_PAN, "D7 Pan"},
        {TARGET_DRUM_8_PAN, "D8 Pan"},
        {TARGET_SYNTH_PAN, "Syn Pan"},
    },
    // Faders (Levels)
    {
        {TARGET_DRUM_1_LEVEL, "D1 Lvl"},
        {TARGET_DRUM_2_LEVEL, "D2 Lvl"},
        {TARGET_DRUM_3_LEVEL, "D3 Lvl"},
        {TARGET_DRUM_4_LEVEL, "D4 Lvl"},
        {TARGET_DRUM_5_LEVEL, "D5 Lvl"},
        {TARGET_DRUM_6_LEVEL, "D6 Lvl"},
        {TARGET_DRUM_7_LEVEL, "D7 Lvl"},
        {TARGET_DRUM_8_LEVEL, "D8 Lvl"},
        {TARGET_SYNTH_LEVEL, "Syn Lvl"},
    }
};

// Bank 2: Synth (Sound Design)
constexpr BankMappings BANK_SYNTH_MAP = {
    "Synth",
    // Encoders
    {
        {TARGET_SYNTH_FILTER_CUTOFF, "Cutoff"},
        {TARGET_SYNTH_FILT_ATTACK, "FltAtk"},
        {TARGET_SYNTH_FILT_DECAY, "FltDcy"},
        {TARGET_SYNTH_OSC2_DETUNE, "Detune"},
        {TARGET_SYNTH_AMP_ATTACK, "AmpAtk"},
        {TARGET_SYNTH_AMP_DECAY, "AmpDcy"},
        {TARGET_SYNTH_AMP_RELEASE, "AmpRel"},
        {TARGET_SYNTH_OSC1_WAVE, "Wave1"},
        {TARGET_SYNTH_OSC2_WAVE, "Wave2"},
    },
    // Faders
    {
        {TARGET_SYNTH_OSC1_LEVEL, "Osc1"},
        {TARGET_SYNTH_OSC2_LEVEL, "Osc2"},
        {TARGET_SYNTH_FILTER_RES, "Reso"},
        {TARGET_SYNTH_FILTER_ENV_AMT, "FltEnv"},
        {TARGET_SYNTH_AMP_SUSTAIN, "AmpSus"},
        {TARGET_SYNTH_FILT_SUSTAIN, "FltSus"},
        {TARGET_SYNTH_FILT_RELEASE, "FltRel"},
        {TARGET_NONE, "---"},
        {TARGET_SYNTH_LEVEL, "Syn Lvl"},
    }
};

// Bank 3: Drums+ (per-voice sound design, Phase 5 — the v1 Sampler-bank
// promise: encoder tunes a drum, the fader under it shapes its decay)
constexpr BankMappings BANK_SAMPLER_MAP = {
    "Drums+",
    // Encoders: per-voice pitch (center = as sampled, ±1 octave)
    {
        {TARGET_DRUM_1_PITCH, "D1 Pit"},
        {TARGET_DRUM_2_PITCH, "D2 Pit"},
        {TARGET_DRUM_3_PITCH, "D3 Pit"},
        {TARGET_DRUM_4_PITCH, "D4 Pit"},
        {TARGET_DRUM_5_PITCH, "D5 Pit"},
        {TARGET_DRUM_6_PITCH, "D6 Pit"},
        {TARGET_DRUM_7_PITCH, "D7 Pit"},
        {TARGET_DRUM_8_PITCH, "D8 Pit"},
        {TARGET_NONE, "---"},
    },
    // Faders: per-voice decay (30 ms .. 3 s, log)
    {
        {TARGET_DRUM_1_DECAY, "D1 Dcy"},
        {TARGET_DRUM_2_DECAY, "D2 Dcy"},
        {TARGET_DRUM_3_DECAY, "D3 Dcy"},
        {TARGET_DRUM_4_DECAY, "D4 Dcy"},
        {TARGET_DRUM_5_DECAY, "D5 Dcy"},
        {TARGET_DRUM_6_DECAY, "D6 Dcy"},
        {TARGET_DRUM_7_DECAY, "D7 Dcy"},
        {TARGET_DRUM_8_DECAY, "D8 Dcy"},
        {TARGET_DRUM_MASTER_LEVEL, "DrumMst"},
    }
};

// All bank mappings array
constexpr const BankMappings* ALL_BANKS[NUM_BANKS] = {
    &BANK_GENERAL_MAP,
    &BANK_MIX_MAP,
    &BANK_SYNTH_MAP,
    &BANK_SAMPLER_MAP
};

/**
 * Fader state for pickup mode
 */
struct FaderState
{
    uint8_t physical_value;   // Last CC value received from hardware
    uint8_t param_value;      // Current parameter value (0-127)
    bool picked_up;           // Is fader tracking the parameter?
    bool needs_pickup;        // Waiting for fader to reach param value?

    void Init()
    {
        physical_value = 64;  // Default center
        param_value = 64;
        picked_up = true;
        needs_pickup = false;
    }

    /**
     * Update fader position and check pickup
     * Returns true if the fader value should be applied to the parameter
     */
    bool Update(uint8_t new_physical)
    {
        physical_value = new_physical;

        if(picked_up)
        {
            // Already tracking - update param value
            param_value = physical_value;
            return true;
        }

        // Check if fader has reached pickup zone
        int diff = static_cast<int>(physical_value) - static_cast<int>(param_value);
        if(diff < 0) diff = -diff;

        if(diff <= PICKUP_TOLERANCE)
        {
            // Picked up!
            picked_up = true;
            needs_pickup = false;
            param_value = physical_value;
            return true;
        }

        // Not picked up yet
        return false;
    }

    /**
     * Mark fader as needing pickup (called on bank switch)
     */
    void RequirePickup()
    {
        picked_up = false;
        needs_pickup = true;
    }

    /**
     * Set current param value (called when loading new bank)
     */
    void SetParamValue(uint8_t value)
    {
        param_value = value;
    }
};

/**
 * Main CC mapping engine with bank support and fader pickup
 */
class Engine
{
  public:
    void Init()
    {
        current_bank_ = BANK_SYNTH;  // Start on Synth bank (most common)
        master_output_ = 1.0f;

        for(uint8_t i = 0; i < NUM_FADERS; i++)
        {
            fader_states_[i].Init();
        }

        for(uint8_t i = 0; i < NUM_ENCODERS; i++)
        {
            encoder_values_[i] = 64;  // Default center
        }
    }

    /**
     * Get current bank
     */
    Bank GetBank() const { return current_bank_; }

    /**
     * Get bank name
     */
    const char* GetBankName() const
    {
        return ALL_BANKS[current_bank_]->bank_name;
    }

    /**
     * Set bank directly
     */
    void SetBank(Bank bank)
    {
        if(bank >= NUM_BANKS)
            return;
        if(bank == current_bank_)
            return;

        current_bank_ = bank;

        // Mark all faders as needing pickup
        for(uint8_t i = 0; i < NUM_FADERS; i++)
        {
            fader_states_[i].RequirePickup();
        }

        bank_changed_ = true;
    }

    /**
     * Handle bank switch CCs.
     * Returns true if the CC was consumed (bank switch OR mod wheel).
     *
     * CC 1 is BOTH the Part 1 button (a lone value-0 event) and the mod
     * wheel (a stream of values ending at 0 when the spring returns).
     * The wheel used to slot-machine the banks (v1 bug, ROADMAP Phase 5):
     * now any nonzero CC 1 marks wheel motion, and a value-0 within
     * WHEEL_GUARD_MS of motion is the wheel coming to rest, not a press.
     */
    static constexpr uint32_t WHEEL_GUARD_MS = 400;

    bool HandleBankSwitch(uint8_t cc, uint8_t value, uint32_t now_ms)
    {
        if(cc == CC_BANK_NEXT)
        {
            if(value != 0)
            {
                last_wheel_ms_ = now_ms;
                wheel_moved_   = true;
                return true; // wheel motion: consumed, never a switch
            }
            if(wheel_moved_ && now_ms - last_wheel_ms_ < WHEEL_GUARD_MS)
            {
                return true; // wheel returning to rest
            }
            uint8_t next = (current_bank_ + 1) % NUM_BANKS;
            SetBank(static_cast<Bank>(next));
            return true;
        }
        else if(cc == CC_BANK_PREV)
        {
            uint8_t prev = (current_bank_ + NUM_BANKS - 1) % NUM_BANKS;
            SetBank(static_cast<Bank>(prev));
            return true;
        }

        return false;
    }

    /**
     * Check and clear bank changed flag
     */
    bool BankChanged()
    {
        bool changed = bank_changed_;
        bank_changed_ = false;
        return changed;
    }

    /**
     * Get fader state for UI
     */
    const FaderState& GetFaderState(uint8_t idx) const
    {
        return fader_states_[idx < NUM_FADERS ? idx : 0];
    }

    /**
     * Get encoder value
     */
    uint8_t GetEncoderValue(uint8_t idx) const
    {
        return encoder_values_[idx < NUM_ENCODERS ? idx : 0];
    }

    /**
     * Get mapping for encoder in current bank
     */
    const ControlMapping& GetEncoderMapping(uint8_t idx) const
    {
        return ALL_BANKS[current_bank_]->encoders[idx < NUM_ENCODERS ? idx : 0];
    }

    /**
     * Get mapping for fader in current bank
     */
    const ControlMapping& GetFaderMapping(uint8_t idx) const
    {
        return ALL_BANKS[current_bank_]->faders[idx < NUM_FADERS ? idx : 0];
    }

    /**
     * Find encoder index by CC number
     * Returns NUM_ENCODERS if not found
     */
    uint8_t FindEncoderIndex(uint8_t cc) const
    {
        for(uint8_t i = 0; i < NUM_ENCODERS; i++)
        {
            if(ENCODER_CCS[i] == cc)
                return i;
        }
        return NUM_ENCODERS;
    }

    /**
     * Find fader index by CC number
     * Returns NUM_FADERS if not found
     */
    uint8_t FindFaderIndex(uint8_t cc) const
    {
        for(uint8_t i = 0; i < NUM_FADERS; i++)
        {
            if(FADER_CCS[i] == cc)
                return i;
        }
        return NUM_FADERS;
    }

    /**
     * Process a CC and get the target parameter and value.
     * now_ms feeds the mod-wheel/Part-1 disambiguation on CC 1.
     * Returns TARGET_NONE if CC was not handled or fader not picked up
     */
    ParamTarget ProcessCC(uint8_t cc, uint8_t value, uint8_t& out_value,
                          uint32_t now_ms)
    {
        // Check for bank switch (also swallows mod-wheel CC 1 traffic)
        if(HandleBankSwitch(cc, value, now_ms))
        {
            return TARGET_NONE;
        }

        // Check if it's an encoder
        uint8_t enc_idx = FindEncoderIndex(cc);
        if(enc_idx < NUM_ENCODERS)
        {
            encoder_values_[enc_idx] = value;
            const ControlMapping& mapping = GetEncoderMapping(enc_idx);
            out_value = value;
            return mapping.target;
        }

        // Check if it's a fader
        uint8_t fader_idx = FindFaderIndex(cc);
        if(fader_idx < NUM_FADERS)
        {
            FaderState& state = fader_states_[fader_idx];
            if(state.Update(value))
            {
                const ControlMapping& mapping = GetFaderMapping(fader_idx);
                out_value = state.param_value;
                return mapping.target;
            }
            return TARGET_NONE;  // Fader not picked up
        }

        return TARGET_NONE;
    }

    /**
     * Set fader param value (for syncing with actual param state)
     */
    void SetFaderParamValue(uint8_t fader_idx, uint8_t value)
    {
        if(fader_idx < NUM_FADERS)
        {
            fader_states_[fader_idx].SetParamValue(value);
        }
    }

    /**
     * Get/set master output level
     */
    float GetMasterOutput() const { return master_output_; }
    void SetMasterOutput(float level) { master_output_ = level; }

  private:
    Bank current_bank_;
    FaderState fader_states_[NUM_FADERS];
    uint8_t encoder_values_[NUM_ENCODERS];
    float master_output_;
    bool bank_changed_ = false;
    // Mod-wheel / Part 1 disambiguation (both live on CC 1)
    uint32_t last_wheel_ms_ = 0;
    bool     wheel_moved_   = false;
};

// ============================================================================
// Helper functions for value conversion
// ============================================================================

// Convert CC value (0-127) to normalized float (0.0-1.0)
inline float CCToNorm(uint8_t value)
{
    return static_cast<float>(value) / 127.0f;
}

// Convert normalized float to CC value
inline uint8_t NormToCC(float value)
{
    return static_cast<uint8_t>(value * 127.0f);
}

// Convert CC value to logarithmic frequency (20-20000 Hz)
inline float CCToFreq(uint8_t value)
{
    float norm = CCToNorm(value);
    return 20.0f * powf(1000.0f, norm);
}

// Convert CC value to time (0.001-5.0 seconds, logarithmic)
inline float CCToTime(uint8_t value)
{
    float norm = CCToNorm(value);
    return 0.001f * powf(5000.0f, norm);
}

// Convert CC value to waveform index (0-3)
inline uint8_t CCToWave(uint8_t value)
{
    return (value * Synth::WAVE_COUNT) / 128;
}

// Convert CC value to semitones (-24 to +24)
inline int8_t CCToSemitones(uint8_t value)
{
    return static_cast<int8_t>((value - 64) * 24 / 64);
}

// Convert CC value to pan (-1.0 to +1.0)
inline float CCToPan(uint8_t value)
{
    return (static_cast<float>(value) - 64.0f) / 64.0f;
}

// Drum voice pitch: center detent = as sampled, sweep = ±1 octave
inline float CCToDrumPitch(uint8_t value)
{
    return powf(2.0f, (static_cast<float>(value) - 64.0f) / 64.0f);
}

// Drum voice decay: CC -> per-sample envelope multiplier for a -60 dB
// decay of 30 ms (fader down) .. 3 s (fader up), log taper @48k
inline float CCToDrumDecay(uint8_t value)
{
    float t_sec = 0.030f * powf(100.0f, static_cast<float>(value) / 127.0f);
    return powf(10.0f, -3.0f / (t_sec * 48000.0f));
}

// Convert pan (-1.0 to +1.0) to CC value
inline uint8_t PanToCC(float pan)
{
    return static_cast<uint8_t>((pan + 1.0f) * 64.0f);
}

/**
 * Legacy: Handle a CC message and apply to synth
 * Returns true if the CC was handled
 * (Kept for backwards compatibility with midi_router)
 */
inline bool HandleSynthCC(uint8_t cc, uint8_t value, Synth::Engine& synth)
{
    switch(cc)
    {
        // Encoders
        case FILTER_CUTOFF:
            synth.SetParam(Synth::PARAM_FILTER_CUTOFF, CCToFreq(value));
            return true;

        case FILTER_RES:
            synth.SetParam(Synth::PARAM_FILTER_RES, CCToNorm(value));
            return true;

        case OSC1_WAVE:
            synth.SetParam(Synth::PARAM_OSC1_WAVE, CCToWave(value));
            return true;

        case OSC2_WAVE:
            synth.SetParam(Synth::PARAM_OSC2_WAVE, CCToWave(value));
            return true;

        case AMP_ATTACK:
            synth.SetParam(Synth::PARAM_AMP_ATTACK, CCToTime(value));
            return true;

        case AMP_DECAY:
            synth.SetParam(Synth::PARAM_AMP_DECAY, CCToTime(value));
            return true;

        case AMP_SUSTAIN:
            synth.SetParam(Synth::PARAM_AMP_SUSTAIN, CCToNorm(value));
            return true;

        case AMP_RELEASE:
            synth.SetParam(Synth::PARAM_AMP_RELEASE, CCToTime(value));
            return true;

        // Faders
        case OSC1_LEVEL:
            synth.SetParam(Synth::PARAM_OSC1_LEVEL, CCToNorm(value));
            return true;

        case OSC2_LEVEL:
            synth.SetParam(Synth::PARAM_OSC2_LEVEL, CCToNorm(value));
            return true;

        case FILT_ENV_AMT:
            synth.SetParam(Synth::PARAM_FILTER_ENV_AMT, CCToNorm(value));
            return true;

        case SYNTH_LEVEL:
            synth.SetParam(Synth::PARAM_LEVEL, CCToNorm(value));
            return true;

        default:
            return false;
    }
}

} // namespace CCMap

#endif // GROOVYDAISY_CC_MAP_H
