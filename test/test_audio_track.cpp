#include "mini_test.h"
#include "audio_track.h"

#include <cstdlib>
#include <cstring>
#include <vector>

using namespace AudioTrack;

// 42 MB pool as on the device, but heap-allocated for the host
static const size_t POOL_SAMPLES = 42UL * 1024 * 1024 / sizeof(int16_t);

struct PoolFixture
{
    std::vector<int16_t> mem;
    GranulePool          pool;
    PoolFixture() : mem(POOL_SAMPLES)
    {
        pool.Init(mem.data(), POOL_SAMPLES);
    }
};

TEST(pool_carve_counts_match_spec_table)
{
    // 42 MB s16 pool = 22,020,096 samples. A 4/4 bar at 60 BPM is
    // 192,000 samples (384 KB) -> 114 granules. (This test corrected the
    // SPEC table, which had double-counted bytes-per-bar.)
    PoolFixture f;
    f.pool.Lock(192000); // 60 BPM
    CHECK_EQ(f.pool.BarsTotal(), 114);

    f.pool.Unlock();
    f.pool.Lock(96000); // 120 BPM
    CHECK_EQ(f.pool.BarsTotal(), 229);

    f.pool.Unlock();
    f.pool.Lock(64000); // 180 BPM
    CHECK_EQ(f.pool.BarsTotal(), 344);

    f.pool.Unlock();
    f.pool.Lock(57600); // 200 BPM -> would be 382, capped by MAX_GRANULES
    CHECK_EQ(f.pool.BarsTotal(), 382);
}

TEST(pool_alloc_free_reuse_no_fragmentation)
{
    PoolFixture f;
    f.pool.Lock(96000);
    uint16_t total = f.pool.BarsTotal();

    uint16_t a[8], b[4], c[8];
    CHECK(f.pool.Alloc(8, a));
    CHECK(f.pool.Alloc(4, b));
    CHECK_EQ(f.pool.BarsFree(), total - 12);

    // Free the FIRST allocation (a hole, in a naive allocator's terms)
    f.pool.Free(a, 8);
    CHECK_EQ(f.pool.BarsFree(), total - 4);

    // A new 8-bar loop must fit — freed bars are interchangeable
    CHECK(f.pool.Alloc(8, c));
    CHECK_EQ(f.pool.BarsFree(), total - 12);
}

TEST(pool_exhaustion_refuses_cleanly)
{
    PoolFixture f;
    f.pool.Lock(96000);
    uint16_t total = f.pool.BarsTotal();

    // Drain the pool with 8-bar allocations
    uint16_t idx[8];
    int      loops = 0;
    while(f.pool.Alloc(8, idx))
    {
        loops++;
    }
    CHECK_EQ(loops, total / 8);
    CHECK(f.pool.BarsFree() < 8);

    // Smaller allocation may still fit; over-ask must not corrupt state
    uint16_t before = f.pool.BarsFree();
    CHECK(!f.pool.Alloc(8, idx));
    CHECK_EQ(f.pool.BarsFree(), before);
}

TEST(pool_unlock_recarve_at_new_tempo)
{
    PoolFixture f;
    f.pool.Lock(96000);
    uint16_t idx[2];
    CHECK(f.pool.Alloc(2, idx));

    // Last audio track deleted -> unlock -> re-lock at a new tempo
    f.pool.Free(idx, 2);
    f.pool.Unlock();
    CHECK(!f.pool.Locked());
    f.pool.Lock(120000);
    CHECK(f.pool.Locked());
    CHECK_EQ(f.pool.BarsFree(), f.pool.BarsTotal());
}

// ---------------------------------------------------------------------------
// CopyJob: simulated ring with known contents
// ---------------------------------------------------------------------------

struct RingFixture
{
    std::vector<int16_t> ring_mem;
    Capture::AudioRing   ring;
    RingFixture(uint32_t capacity) : ring_mem(capacity)
    {
        ring.Init(ring_mem.data(), capacity);
    }
    // Write n samples with a recognizable pattern; anchor each bar line
    void Fill(uint32_t n, uint32_t spb, uint32_t start_tick = 0)
    {
        for(uint32_t i = 0; i < n; i++)
        {
            if(i % spb == 0)
            {
                ring.AnchorBar(start_tick + (i / spb) * 384);
            }
            ring.Write((float)((i % 1000) - 500) / 32768.0f * 128.0f);
        }
    }
};

TEST(copyjob_basic_window_lands_in_loop_position_order)
{
    const uint32_t spb = 4800; // tiny "bar" for test speed
    PoolFixture    pf;
    pf.pool.Lock(spb);

    RingFixture rf(spb * 10);
    rf.Fill(spb * 5, spb); // 5 bars written, anchors at ticks 0,384,...

    // Capture 2 bars ending at bar 4 (tick 1536): global bars 2 and 3.
    // Loop positions: bar 2 -> slot 0 (2%2), bar 3 -> slot 1.
    uint16_t chain[2];
    CHECK(pf.pool.Alloc(2, chain));

    uint32_t ring_start = 0;
    CHECK(rf.ring.FindAnchor(2 * 384, ring_start));
    CHECK_EQ(ring_start, spb * 2);

    CopyJob job;
    job.Start(&rf.ring, &pf.pool, chain, 2, 2 * 384, ring_start);
    CopyResult res = CopyResult::Working;
    int        steps = 0;
    while(res == CopyResult::Working)
    {
        res = job.Step(1024);
        steps++;
    }
    CHECK_EQ((int)res, (int)CopyResult::Done);
    CHECK(steps > 1); // actually incremental

    // Verify contents: chain[0] holds global bar 2's samples
    for(uint32_t i = 0; i < spb; i += 997)
    {
        int16_t expect = rf.ring.At(spb * 2 + i);
        CHECK_EQ(pf.pool.GranuleMem(chain[0])[i], expect);
    }
    // chain[1] holds global bar 3
    for(uint32_t i = 0; i < spb; i += 997)
    {
        CHECK_EQ(pf.pool.GranuleMem(chain[1])[i], rf.ring.At(spb * 3 + i));
    }
}

TEST(copyjob_odd_start_bar_phase_preserved)
{
    // Window of 2 bars ending at bar 5: global bars 3,4 -> slots 1,0.
    const uint32_t spb = 4800;
    PoolFixture    pf;
    pf.pool.Lock(spb);
    RingFixture rf(spb * 10);
    rf.Fill(spb * 6, spb);

    uint16_t chain[2];
    CHECK(pf.pool.Alloc(2, chain));
    uint32_t ring_start = 0;
    CHECK(rf.ring.FindAnchor(3 * 384, ring_start));

    CopyJob job;
    job.Start(&rf.ring, &pf.pool, chain, 2, 3 * 384, ring_start);
    while(job.Step(4096) == CopyResult::Working) {}

    // Global bar 3 (odd) must land in chain slot 3%2 = 1
    CHECK_EQ(pf.pool.GranuleMem(chain[1])[100], rf.ring.At(spb * 3 + 100));
    // Global bar 4 -> slot 0
    CHECK_EQ(pf.pool.GranuleMem(chain[0])[100], rf.ring.At(spb * 4 + 100));
}

TEST(copyjob_ring_wraparound_copy)
{
    // Ring smaller than total written: window crosses the physical wrap
    const uint32_t spb = 4800;
    PoolFixture    pf;
    pf.pool.Lock(spb);
    RingFixture rf(spb * 3); // tiny ring: 3 bars capacity
    rf.Fill(spb * 8, spb);   // 8 bars written -> heavy wrapping

    // Capture the last 2 bars (global bars 6,7; end tick 8*384)
    uint16_t chain[2];
    CHECK(pf.pool.Alloc(2, chain));
    uint32_t ring_start = 0;
    CHECK(rf.ring.FindAnchor(6 * 384, ring_start));
    CHECK_EQ(ring_start, spb * 6);

    CopyJob job;
    job.Start(&rf.ring, &pf.pool, chain, 2, 6 * 384, ring_start);
    while(job.Step(4096) == CopyResult::Working) {}

    CHECK_EQ(pf.pool.GranuleMem(chain[0])[7], rf.ring.At(spb * 6 + 7));
    CHECK_EQ(pf.pool.GranuleMem(chain[1])[7], rf.ring.At(spb * 7 + 7));
}

TEST(copyjob_overrun_aborts)
{
    const uint32_t spb = 4800;
    PoolFixture    pf;
    pf.pool.Lock(spb);
    RingFixture rf(spb * 3);
    rf.Fill(spb * 3, spb);

    uint16_t chain[2];
    CHECK(pf.pool.Alloc(2, chain));
    uint32_t ring_start = 0;
    CHECK(rf.ring.FindAnchor(0, ring_start));

    CopyJob job;
    job.Start(&rf.ring, &pf.pool, chain, 2, 0, ring_start);
    CHECK_EQ((int)job.Step(64), (int)CopyResult::Working);

    // Simulate the writer lapping the unread window
    for(uint32_t i = 0; i < spb * 4; i++)
    {
        rf.ring.Write(0.1f);
    }
    CHECK_EQ((int)job.Step(64), (int)CopyResult::Overrun);
    CHECK(!job.Active());
}

TEST(copyjob_peaks_reflect_signal)
{
    const uint32_t spb = 2400; // 100 buckets of 100... 2400/24 = 100
    PoolFixture    pf;
    pf.pool.Lock(spb);

    std::vector<int16_t> rmem(spb * 4);
    Capture::AudioRing   ring;
    ring.Init(rmem.data(), spb * 4);

    // Bar 0: silence. Bar 1: loud burst in its middle.
    ring.AnchorBar(0);
    for(uint32_t i = 0; i < spb; i++)
        ring.Write(0.0f);
    ring.AnchorBar(384);
    for(uint32_t i = 0; i < spb; i++)
        ring.Write(i > spb / 3 && i < spb / 2 ? 0.9f : 0.0f);

    uint16_t chain[2];
    CHECK(pf.pool.Alloc(2, chain));
    uint32_t ring_start = 0;
    CHECK(ring.FindAnchor(0, ring_start));

    CopyJob job;
    job.Start(&ring, &pf.pool, chain, 2, 0, ring_start);
    while(job.Step(4096) == CopyResult::Working) {}

    uint8_t  peaks[MAX_PEAKS];
    uint16_t count = 0;
    job.GetPeaks(peaks, count);
    CHECK_EQ(count, 48); // 2 bars * 24

    // Bar 0 buckets silent, bar 1 has loud buckets
    int loud = 0;
    for(int i = 0; i < 24; i++)
    {
        CHECK_EQ(peaks[i], 0);
        if(peaks[24 + i] > 100)
            loud++;
    }
    CHECK(loud >= 2);
}
