#include "mini_test.h"
#include "clock.h"

using Clock::Engine;
using Clock::TickBlock;

static const float  kSampleRate = 48000.0f;
static const size_t kBlock      = 48;

// Advance the clock in audio-sized blocks for `seconds`, counting ticks.
static long RunFor(Engine& e, float seconds)
{
    long      ticks   = 0;
    long      samples = (long)(seconds * kSampleRate);
    TickBlock tb;
    for(long i = 0; i < samples; i += kBlock)
    {
        e.Advance(kBlock, tb);
        ticks += (long)tb.count;
    }
    return ticks;
}

TEST(clock_no_ticks_while_stopped)
{
    Engine e;
    e.Init(kSampleRate);
    CHECK_EQ(RunFor(e, 1.0f), 0);
}

TEST(clock_count_in_one_bar_before_grid)
{
    Engine e;
    e.Init(kSampleRate); // count-in defaults ON
    CHECK(e.SetBpm(120.0f));
    e.Play();
    CHECK(e.InPreroll());

    // Collect ticks over 4s @120 = 2 bars of wall time
    TickBlock tb;
    long preroll_ticks = 0, normal_ticks = 0;
    for(long i = 0; i < (long)(4.0f * kSampleRate); i += kBlock)
    {
        e.Advance(kBlock, tb);
        for(size_t t = 0; t < tb.count; t++)
        {
            if(tb.preroll[t])
                preroll_ticks++;
            else
                normal_ticks++;
        }
    }
    // Exactly one bar of count-in ticks, then the grid runs
    CHECK_EQ(preroll_ticks, 384);
    CHECK_NEAR(normal_ticks, 384, 2); // remaining ~1 bar of the 4s
    CHECK(!e.InPreroll());
    // The grid itself did not advance during the count-in
    CHECK_NEAR(e.NowTick(), 384, 2);
}

TEST(clock_count_in_disabled_starts_immediately)
{
    Engine e;
    e.Init(kSampleRate);
    e.SetCountIn(false);
    e.Play();
    CHECK(!e.InPreroll());
    CHECK_NEAR(RunFor(e, 1.0f), 192, 2); // ticks flow from the start
}

TEST(clock_stop_during_count_in_cancels_it)
{
    Engine e;
    e.Init(kSampleRate);
    e.Play();
    TickBlock tb;
    e.Advance(4800, tb); // 0.1 s into the count-in
    e.Stop();
    CHECK(!e.InPreroll());
    CHECK_EQ(e.NowTick(), 0u); // grid never started
}

TEST(clock_tick_rates)
{
    // 60 BPM: 96/s. 120: 192/s. 200: 320/s.
    struct Case { float bpm; long expect; };
    Case cases[] = {{60.0f, 960}, {120.0f, 1920}, {200.0f, 3200}};
    for(auto& c : cases)
    {
        Engine e;
        e.Init(kSampleRate);
        CHECK(e.SetBpm(c.bpm));
        e.Play();
        CHECK_NEAR(RunFor(e, 10.0f), c.expect, 2);
    }
}

TEST(clock_tick_is_monotonic_no_pattern_wrap)
{
    Engine e;
    e.Init(kSampleRate);
    e.SetCountIn(false); // testing tick monotonicity, not the count-in
    e.Play();
    RunFor(e, 30.0f);
    // 30s @120 BPM = 5760 ticks — far beyond v1's 1536-tick pattern wrap.
    CHECK_NEAR(e.NowTick(), 5760, 2);
}

TEST(clock_frame_offsets_ordered_within_block)
{
    Engine e;
    e.Init(kSampleRate);
    e.SetCountIn(false); // tick numbering restarts at the preroll boundary
    CHECK(e.SetBpm(200.0f));
    e.Play();
    TickBlock tb;
    for(int i = 0; i < 10000; i++)
    {
        e.Advance(256, tb); // large block can hold >1 tick at 200 BPM
        for(size_t t = 1; t < tb.count; t++)
        {
            CHECK(tb.frame[t] > tb.frame[t - 1]);
            CHECK_EQ(tb.tick[t], tb.tick[t - 1] + 1);
        }
    }
}

TEST(clock_rewind_resets_stop_preserves)
{
    Engine e;
    e.Init(kSampleRate);
    e.SetCountIn(false); // testing stop/rewind, not the count-in
    e.Play();
    RunFor(e, 1.0f);
    uint32_t at = e.NowTick();
    CHECK(at > 0);

    e.Stop();
    CHECK_EQ(RunFor(e, 1.0f), 0);
    CHECK_EQ(e.NowTick(), at);

    e.Play();
    RunFor(e, 0.5f);
    CHECK(e.NowTick() > at);

    e.Rewind();
    CHECK_EQ(e.NowTick(), 0);
    CHECK(!e.Playing());
}

TEST(clock_bpm_clamped_to_spec_range)
{
    Engine e;
    e.Init(kSampleRate);
    CHECK(e.SetBpm(30.0f));
    CHECK_NEAR(e.Bpm(), Clock::MIN_BPM, 0.001);
    CHECK(e.SetBpm(500.0f));
    CHECK_NEAR(e.Bpm(), Clock::MAX_BPM, 0.001);
}

TEST(clock_tap_tempo_average)
{
    Engine e;
    e.Init(kSampleRate);
    // Taps 500 ms apart -> 120 BPM
    uint32_t t = 10000;
    for(int i = 0; i < 5; i++)
    {
        CHECK(e.Tap(t));
        t += 500;
    }
    CHECK_NEAR(e.Bpm(), 120.0f, 0.5);

    // Faster taps: 300 ms -> 200 BPM
    t += 10000; // exceed timeout, sequence restarts
    for(int i = 0; i < 5; i++)
    {
        CHECK(e.Tap(t));
        t += 300;
    }
    CHECK_NEAR(e.Bpm(), 200.0f, 1.0);
}

TEST(clock_tap_timeout_starts_fresh)
{
    Engine e;
    e.Init(kSampleRate);
    CHECK(e.Tap(1000));
    CHECK(e.Tap(1500)); // 500ms -> 120
    CHECK_NEAR(e.Bpm(), 120.0f, 0.5);
    // A tap long after the timeout must not fold the huge gap into the avg
    CHECK(e.Tap(60000));
    CHECK_NEAR(e.Bpm(), 120.0f, 0.5); // single fresh tap: no change yet
}

TEST(clock_tempo_lock_refuses_changes)
{
    Engine e;
    e.Init(kSampleRate);
    CHECK(e.SetBpm(100.0f));
    e.SetLocked(true);
    CHECK(!e.SetBpm(140.0f));
    CHECK(!e.Tap(5000));
    CHECK_NEAR(e.Bpm(), 100.0f, 0.001);
    e.SetLocked(false);
    CHECK(e.SetBpm(140.0f));
    CHECK_NEAR(e.Bpm(), 140.0f, 0.001);
}

TEST(clock_nearest_bar_rounding)
{
    constexpr auto NearestBar = Clock::Engine::NearestBar;
    CHECK_EQ(Clock::Engine::Bar(0), 0);
    CHECK_EQ(NearestBar(0), 0);
    CHECK_EQ(NearestBar(100), 0);          // < half bar: rounds down
    CHECK_EQ(NearestBar(191), 0);
    CHECK_EQ(NearestBar(192), 384);        // half bar: rounds up
    CHECK_EQ(NearestBar(383), 384);
    CHECK_EQ(NearestBar(384), 384);
    CHECK_EQ(NearestBar(384 * 3 + 50), 384 * 3);
}

TEST(clock_samples_per_bar)
{
    Engine e;
    e.Init(kSampleRate);
    CHECK(e.SetBpm(120.0f));
    // 120 BPM: 2 s/bar = 96000 samples
    CHECK_EQ(e.SamplesPerBar(), 96000);
    CHECK(e.SetBpm(60.0f));
    CHECK_EQ(e.SamplesPerBar(), 192000);
}
