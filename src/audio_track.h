#pragma once
#ifndef GROOVYDAISY_AUDIO_TRACK_H
#define GROOVYDAISY_AUDIO_TRACK_H

#include <stdint.h>
#include <stddef.h>
#include "capture.h"

/**
 * GroovyDaisy audio loop storage (v2) — the granule pool.
 *
 * Tempo locks when the first audio loop is captured, making
 * `samples_per_bar` a constant; the pool is then carved into uniform
 * 1-bar granules and every audio loop is a chain of granules (SPEC.md).
 * This eliminates fragmentation by construction: any freed bar fits any
 * future loop. Undo/delete FREE memory — there are no shadow buffers.
 *
 * Host-compilable: memory arrives via Init(ptr, size); no libDaisy.
 *
 * Capture commit is a CopyJob: the audio callback only ever writes the
 * ring; the main loop copies the anchored window into granules in small
 * steps (and computes waveform peaks for the arrange lanes as it goes).
 * The writer advances at 48k samples/s while the copier moves millions/s,
 * so overrun is impossible in practice — but it's checked, and an
 * overrun aborts the capture rather than committing garbage.
 */

namespace AudioTrack
{

constexpr uint16_t MAX_GRANULES   = 512; // covers 42 MB at 200 BPM bars
constexpr uint8_t  PEAKS_PER_BAR  = 24;  // waveform buckets for the UI
constexpr uint8_t  MAX_BARS       = 8;
constexpr uint16_t MAX_PEAKS      = PEAKS_PER_BAR * MAX_BARS;

class GranulePool
{
  public:
    void Init(int16_t* mem, size_t total_samples)
    {
        mem_           = mem;
        total_samples_ = total_samples;
        locked_        = false;
        granule_count_ = 0;
        free_count_    = 0;
    }

    /**
     * Tempo lock: carve the pool into 1-bar granules of exactly
     * `samples_per_bar`. Only legal when no chains are outstanding.
     */
    void Lock(uint32_t samples_per_bar)
    {
        samples_per_bar_ = samples_per_bar;
        uint32_t n       = (uint32_t)(total_samples_ / samples_per_bar);
        if(n > MAX_GRANULES)
            n = MAX_GRANULES;
        granule_count_ = (uint16_t)n;
        free_count_    = granule_count_;
        for(uint16_t i = 0; i < granule_count_; i++)
        {
            freelist_[i] = i;
        }
        locked_ = true;
    }

    /** All audio tracks gone: tempo may move again; re-carve on next lock. */
    void Unlock()
    {
        locked_        = false;
        granule_count_ = 0;
        free_count_    = 0;
    }

    bool Locked() const { return locked_; }

    /** Exact-size allocation of `bars` granules into out_idx. */
    bool Alloc(uint8_t bars, uint16_t* out_idx)
    {
        if(!locked_ || free_count_ < bars)
        {
            return false;
        }
        for(uint8_t i = 0; i < bars; i++)
        {
            out_idx[i] = freelist_[--free_count_];
        }
        return true;
    }

    void Free(const uint16_t* idx, uint8_t bars)
    {
        for(uint8_t i = 0; i < bars; i++)
        {
            if(free_count_ < granule_count_)
            {
                freelist_[free_count_++] = idx[i];
            }
        }
    }

    uint16_t BarsFree() const { return free_count_; }
    uint16_t BarsTotal() const { return granule_count_; }
    uint32_t SamplesPerBar() const { return samples_per_bar_; }

    int16_t*       GranuleMem(uint16_t idx) { return mem_ + (size_t)idx * samples_per_bar_; }
    const int16_t* GranuleMem(uint16_t idx) const
    {
        return mem_ + (size_t)idx * samples_per_bar_;
    }

  private:
    int16_t* mem_            = nullptr;
    size_t   total_samples_  = 0;
    uint32_t samples_per_bar_ = 0;
    bool     locked_         = false;
    uint16_t granule_count_  = 0;
    uint16_t free_count_     = 0;
    uint16_t freelist_[MAX_GRANULES];
};

enum class CopyResult : uint8_t
{
    Working = 0,
    Done,
    Overrun, // ring lapped the unread window — abort, free, refuse
};

/**
 * Incremental ring->granules copy, stepped from the main loop. Granules
 * are filled in LOOP-POSITION order: the window's global bar B lands in
 * chain slot B % bars, preserving each bar's phase against the global
 * grid (the same rule as MIDI capture's % length rebasing).
 *
 * Waveform peaks (max |sample| per bucket) accumulate during the copy —
 * the UI's guitar lanes cost nothing extra.
 */
class CopyJob
{
  public:
    void Start(const Capture::AudioRing* ring, GranulePool* pool,
               const uint16_t* chain, uint8_t bars, uint32_t start_tick,
               uint32_t ring_start_pos)
    {
        ring_       = ring;
        pool_       = pool;
        bars_       = bars;
        start_bar_  = start_tick / 384;
        ring_start_ = ring_start_pos;
        copied_     = 0;
        total_      = (uint32_t)bars * pool->SamplesPerBar();
        for(uint8_t i = 0; i < bars; i++)
        {
            chain_[i] = chain[i];
        }
        for(uint16_t i = 0; i < MAX_PEAKS; i++)
        {
            peaks_[i] = 0;
        }
        active_ = true;
    }

    bool Active() const { return active_; }

    /** Copy up to `step_samples`; call repeatedly from the main loop. */
    CopyResult Step(uint32_t step_samples)
    {
        if(!active_)
        {
            return CopyResult::Done;
        }

        // Overrun check: everything not yet copied must still be younger
        // than one ring length behind the writer
        uint32_t wpos = ring_->WritePos();
        if(wpos - (ring_start_ + copied_) > ring_->Capacity())
        {
            active_ = false;
            return CopyResult::Overrun;
        }

        const uint32_t spb         = pool_->SamplesPerBar();
        const uint32_t bucket_size = spb / PEAKS_PER_BAR;
        uint32_t       n           = step_samples;
        if(n > total_ - copied_)
        {
            n = total_ - copied_;
        }

        for(uint32_t i = 0; i < n; i++)
        {
            uint32_t off      = copied_ + i;
            uint32_t win_bar  = off / spb; // bar index within the window
            uint32_t in_bar   = off % spb;
            // Loop-position placement: global bar -> slot (global % bars)
            uint32_t slot_bar = (start_bar_ + win_bar) % bars_;
            int16_t  s        = ring_->At(ring_start_ + off);
            pool_->GranuleMem(chain_[slot_bar])[in_bar] = s;

            uint16_t bucket = (uint16_t)(slot_bar * PEAKS_PER_BAR
                                         + (bucket_size ? in_bar / bucket_size
                                                        : 0));
            if(bucket < MAX_PEAKS)
            {
                uint16_t a = (uint16_t)(s < 0 ? -(int32_t)s : s);
                if(a > peaks_[bucket])
                    peaks_[bucket] = a;
            }
        }
        copied_ += n;

        if(copied_ >= total_)
        {
            active_ = false;
            return CopyResult::Done;
        }
        return CopyResult::Working;
    }

    /** Peaks as u8 (0..255), loop-position order; valid after Done. */
    void GetPeaks(uint8_t* out, uint16_t& count) const
    {
        count = (uint16_t)(bars_ * PEAKS_PER_BAR);
        for(uint16_t i = 0; i < count; i++)
        {
            out[i] = (uint8_t)(peaks_[i] >> 7); // 15-bit -> 8-bit
        }
    }

    uint8_t Bars() const { return bars_; }

  private:
    const Capture::AudioRing* ring_ = nullptr;
    GranulePool*              pool_ = nullptr;
    uint16_t                  chain_[MAX_BARS];
    uint8_t                   bars_      = 0;
    uint32_t                  start_bar_ = 0;
    uint32_t                  ring_start_ = 0;
    uint32_t                  copied_    = 0;
    uint32_t                  total_     = 0;
    uint16_t                  peaks_[MAX_PEAKS];
    bool                      active_ = false;
};

} // namespace AudioTrack

#endif // GROOVYDAISY_AUDIO_TRACK_H
