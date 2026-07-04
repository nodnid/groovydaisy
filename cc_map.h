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

// Bank 0: General (Master Controls)
constexpr BankMappings BANK_GENERAL_MAP = {
    "General",
    // Encoders (mostly unused in General)
    {
        {TARGET_NONE, "---"},
        {TARGET_NONE, "---"},
        {TARGET_NONE, "---"},
        {TARGET_NONE, "---"},
        {TARGET_NONE, "---"},
        {TARGET_NONE, "---"},
        {TARGET_NONE, "---"},
        {TARGET_NONE, "---"},
        {TARGET_NONE, "---"},
    },
    // Faders
    {
        {TARGET_DRUM_MASTER_LEVEL, "DrumMst"},
        {TARGET_SYNTH_MASTER_LEVEL, "SynthMst"},
        {TARGET_NONE, "---"},
        {TARGET_NONE, "---"},
        {TARGET_NONE, "---"},
        {TARGET_NONE, "---"},
        {TARGET_SYNTH_VEL_TO_AMP, "Vel>Amp"},
        {TARGET_SYNTH_VEL_TO_FILTER, "Vel>Flt"},
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

// Bank 3: Sampler (Per-Drum Sound Design - future)
constexpr BankMappings BANK_SAMPLER_MAP = {
    "Sampler",
    // Encoders (per-drum params - future expansion)
    {
        {TARGET_NONE, "Pitch"},      // Future: selected drum pitch
        {TARGET_NONE, "Decay"},      // Future: selected drum decay
        {TARGET_NONE, "Filter"},     // Future: selected drum filter
        {TARGET_NONE, "FltRes"},     // Future: selected drum filter res
        {TARGET_NONE, "Swing"},      // Future: swing amount
        {TARGET_NONE, "---"},
        {TARGET_NONE, "---"},
        {TARGET_NONE, "---"},
        {TARGET_NONE, "---"},
    },
    // Faders (drum levels for reference)
    {
        {TARGET_DRUM_1_LEVEL, "D1 Lvl"},
        {TARGET_DRUM_2_LEVEL, "D2 Lvl"},
        {TARGET_DRUM_3_LEVEL, "D3 Lvl"},
        {TARGET_DRUM_4_LEVEL, "D4 Lvl"},
        {TARGET_DRUM_5_LEVEL, "D5 Lvl"},
        {TARGET_DRUM_6_LEVEL, "D6 Lvl"},
        {TARGET_DRUM_7_LEVEL, "D7 Lvl"},
        {TARGET_DRUM_8_LEVEL, "D8 Lvl"},
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
     * Handle bank switch CCs
     * Returns true if CC was a bank switch command
     */
    bool HandleBankSwitch(uint8_t cc, uint8_t value)
    {
        // KeyLab Essential sends CC 1/2 with value=0 on button press
        // Just trigger on any CC 1/2 event regardless of value
        (void)value;  // Unused - KeyLab always sends 0

        if(cc == CC_BANK_NEXT)
        {
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
     * Process a CC and get the target parameter and value
     * Returns TARGET_NONE if CC was not handled or fader not picked up
     */
    ParamTarget ProcessCC(uint8_t cc, uint8_t value, uint8_t& out_value)
    {
        // Check for bank switch
        if(HandleBankSwitch(cc, value))
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
