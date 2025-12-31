#pragma once
#ifndef GROOVYDAISY_AUTOMATION_H
#define GROOVYDAISY_AUTOMATION_H

#include <stdint.h>
#include "cc_map.h"

/**
 * GroovyDaisy CC Automation
 *
 * Records and plays back CC automation with blend/offset mode:
 * - During playback, live knob movements add/subtract from recorded values
 * - Base position captured when playback starts or recording ends
 * - Supports thinning to avoid recording redundant values
 */

namespace Automation
{

// Maximum automation points per CC
constexpr uint16_t MAX_AUTO_POINTS = 256;

// Number of CCs we track for automation (filter cutoff, res, etc.)
constexpr uint8_t NUM_AUTO_CCS = 8;

// CCs that can be automated (in priority order)
constexpr uint8_t AUTO_CCS[NUM_AUTO_CCS] = {
    CCMap::FILTER_CUTOFF,  // 74 - most commonly automated
    CCMap::FILTER_RES,     // 71
    CCMap::AMP_ATTACK,     // 93
    CCMap::AMP_DECAY,      // 18
    CCMap::AMP_SUSTAIN,    // 19
    CCMap::AMP_RELEASE,    // 16
    CCMap::FILT_ENV_AMT,   // 79
    CCMap::SYNTH_LEVEL,    // 85
};

// Minimum ticks between recorded points (thinning)
constexpr uint16_t MIN_RECORD_INTERVAL = 6;  // ~15.6ms at 96 PPQN, 120 BPM

// Minimum value change to record a new point
constexpr uint8_t MIN_VALUE_CHANGE = 2;

/**
 * Single automation point (tick + value)
 * CC number is implicit from which track it's in
 */
struct AutoPoint
{
    uint32_t tick;
    uint8_t  value;
};

/**
 * Automation track for a single CC
 */
struct AutoTrack
{
    AutoPoint points[MAX_AUTO_POINTS];
    uint16_t  point_count;
    uint16_t  playback_index;
    uint8_t   last_recorded_value;
    uint32_t  last_recorded_tick;

    void Clear()
    {
        point_count         = 0;
        playback_index      = 0;
        last_recorded_value = 64;  // Middle value
        last_recorded_tick  = 0;
    }

    void ResetPlayback() { playback_index = 0; }
};

/**
 * CC automation engine with blend/offset support
 */
class Engine
{
  public:
    void Init(uint32_t pattern_length)
    {
        pattern_length_ = pattern_length;
        last_tick_      = 0;
        blend_enabled_  = true;

        // Clear all tracks
        for(uint8_t i = 0; i < NUM_AUTO_CCS; i++)
        {
            tracks_[i].Clear();
            base_values_[i]    = 64;  // Default base (middle)
            current_values_[i] = 64;  // Current knob position
        }
    }

    /**
     * Get index for a CC number in our automation arrays
     * Returns NUM_AUTO_CCS if not found (not automated)
     */
    uint8_t GetCCIndex(uint8_t cc) const
    {
        for(uint8_t i = 0; i < NUM_AUTO_CCS; i++)
        {
            if(AUTO_CCS[i] == cc)
                return i;
        }
        return NUM_AUTO_CCS;  // Not found
    }

    /**
     * Check if a CC is automated
     */
    bool IsAutomatedCC(uint8_t cc) const { return GetCCIndex(cc) < NUM_AUTO_CCS; }

    /**
     * Record a CC value at the given tick
     * Uses thinning to avoid recording redundant points
     */
    void RecordCC(uint32_t tick, uint8_t cc, uint8_t value)
    {
        uint8_t idx = GetCCIndex(cc);
        if(idx >= NUM_AUTO_CCS)
            return;

        AutoTrack& track = tracks_[idx];

        // Update current value (for blend calculation)
        current_values_[idx] = value;

        // Thinning: skip if too close to last point AND value hasn't changed much
        if(track.point_count > 0)
        {
            uint32_t tick_diff  = tick - track.last_recorded_tick;
            int      value_diff = abs(static_cast<int>(value) -
                                      static_cast<int>(track.last_recorded_value));

            if(tick_diff < MIN_RECORD_INTERVAL && value_diff < MIN_VALUE_CHANGE)
            {
                return;  // Skip this point
            }
        }

        // Check capacity
        if(track.point_count >= MAX_AUTO_POINTS)
            return;

        // Find insertion point (maintain sorted order)
        uint16_t insert_pos = track.point_count;
        for(uint16_t i = 0; i < track.point_count; i++)
        {
            if(track.points[i].tick > tick)
            {
                insert_pos = i;
                break;
            }
        }

        // Shift points to make room
        for(uint16_t i = track.point_count; i > insert_pos; i--)
        {
            track.points[i] = track.points[i - 1];
        }

        // Insert new point
        track.points[insert_pos].tick  = tick;
        track.points[insert_pos].value = value;
        track.point_count++;

        // Update last recorded
        track.last_recorded_tick  = tick;
        track.last_recorded_value = value;
    }

    /**
     * Update current CC value (from live input)
     * Called even when not recording to track knob position for blend
     */
    void UpdateCurrentValue(uint8_t cc, uint8_t value)
    {
        uint8_t idx = GetCCIndex(cc);
        if(idx < NUM_AUTO_CCS)
        {
            current_values_[idx] = value;
        }
    }

    /**
     * Capture base values for blend mode
     * Call this when playback starts
     */
    void CaptureBaseValues()
    {
        for(uint8_t i = 0; i < NUM_AUTO_CCS; i++)
        {
            base_values_[i] = current_values_[i];
        }
    }

    /**
     * Process automation playback for current tick
     * Returns the effective CC value with blend applied
     *
     * @param current_tick Current transport tick
     * @param cc CC number to get
     * @param has_value Set to true if automation has a value for this tick
     * @return Effective CC value (recorded + blend offset)
     */
    uint8_t ProcessPlayback(uint32_t current_tick, uint8_t cc, bool& has_value)
    {
        has_value = false;
        uint8_t idx = GetCCIndex(cc);
        if(idx >= NUM_AUTO_CCS)
            return current_values_[idx < NUM_AUTO_CCS ? idx : 0];

        AutoTrack& track = tracks_[idx];

        // Detect pattern loop
        if(current_tick < last_tick_)
        {
            ResetPlayback();
        }

        // Find recorded value at current tick
        uint8_t recorded_value = 64;  // Default if no automation

        // No automation points - return current value
        if(track.point_count == 0)
        {
            return current_values_[idx];
        }

        // Find the most recent point at or before current tick
        bool found = false;
        for(uint16_t i = 0; i < track.point_count; i++)
        {
            if(track.points[i].tick <= current_tick)
            {
                recorded_value = track.points[i].value;
                found = true;
            }
            else
            {
                break;  // Past current tick
            }
        }

        if(!found)
        {
            // No point yet - use first point's value or default
            return current_values_[idx];
        }

        has_value = true;

        // Apply blend offset if enabled
        if(blend_enabled_)
        {
            // Offset = current - base
            int offset = static_cast<int>(current_values_[idx]) -
                         static_cast<int>(base_values_[idx]);

            // Apply offset to recorded value
            int result = static_cast<int>(recorded_value) + offset;

            // Clamp to MIDI range
            if(result < 0)
                result = 0;
            if(result > 127)
                result = 127;

            return static_cast<uint8_t>(result);
        }

        return recorded_value;
    }

    /**
     * Process all automation for the current tick
     * Calls the provided callback with CC values that have automation
     */
    typedef void (*AutoPlaybackCallback)(uint8_t cc, uint8_t value);

    void Process(uint32_t current_tick, AutoPlaybackCallback callback)
    {
        if(callback == nullptr)
            return;

        // Update tick tracking for loop detection
        if(current_tick < last_tick_)
        {
            ResetPlayback();
        }
        last_tick_ = current_tick;

        // Process each automated CC
        for(uint8_t i = 0; i < NUM_AUTO_CCS; i++)
        {
            AutoTrack& track = tracks_[i];

            // Skip empty tracks
            if(track.point_count == 0)
                continue;

            // Check if there's an automation point at this tick
            // We need to scan through points to find current value
            while(track.playback_index < track.point_count &&
                  track.points[track.playback_index].tick <= current_tick)
            {
                // Get the value at this point
                uint8_t value = track.points[track.playback_index].value;

                // Apply blend offset
                if(blend_enabled_)
                {
                    int offset = static_cast<int>(current_values_[i]) -
                                 static_cast<int>(base_values_[i]);
                    int result = static_cast<int>(value) + offset;
                    if(result < 0)
                        result = 0;
                    if(result > 127)
                        result = 127;
                    value = static_cast<uint8_t>(result);
                }

                // Only trigger callback when we hit the exact tick
                if(track.points[track.playback_index].tick == current_tick)
                {
                    callback(AUTO_CCS[i], value);
                }

                track.playback_index++;
            }
        }
    }

    /**
     * Clear all automation
     */
    void Clear()
    {
        for(uint8_t i = 0; i < NUM_AUTO_CCS; i++)
        {
            tracks_[i].Clear();
        }
    }

    /**
     * Clear automation for a specific CC
     */
    void ClearCC(uint8_t cc)
    {
        uint8_t idx = GetCCIndex(cc);
        if(idx < NUM_AUTO_CCS)
        {
            tracks_[idx].Clear();
        }
    }

    /**
     * Reset playback indices
     */
    void ResetPlayback()
    {
        for(uint8_t i = 0; i < NUM_AUTO_CCS; i++)
        {
            tracks_[i].ResetPlayback();
        }
        last_tick_ = 0;
    }

    /**
     * Enable/disable blend mode
     */
    void SetBlendEnabled(bool enabled) { blend_enabled_ = enabled; }
    bool IsBlendEnabled() const { return blend_enabled_; }

    /**
     * Get total automation point count
     */
    uint16_t GetTotalPointCount() const
    {
        uint16_t total = 0;
        for(uint8_t i = 0; i < NUM_AUTO_CCS; i++)
        {
            total += tracks_[i].point_count;
        }
        return total;
    }

    /**
     * Get point count for a specific CC
     */
    uint16_t GetCCPointCount(uint8_t cc) const
    {
        uint8_t idx = GetCCIndex(cc);
        if(idx < NUM_AUTO_CCS)
        {
            return tracks_[idx].point_count;
        }
        return 0;
    }

  private:
    AutoTrack tracks_[NUM_AUTO_CCS];
    uint8_t   base_values_[NUM_AUTO_CCS];     // Captured at playback start
    uint8_t   current_values_[NUM_AUTO_CCS];  // Current knob positions
    uint32_t  pattern_length_;
    uint32_t  last_tick_;
    bool      blend_enabled_;
};

} // namespace Automation

#endif // GROOVYDAISY_AUTOMATION_H
