#include "mini_test.h"
#include "transport.h"

using namespace Transport;

static const float kSampleRate = 48000.0f;

// Run the engine for `seconds` of audio, returning total ticks generated.
static long RunFor(Engine& e, float seconds)
{
    long ticks   = 0;
    long samples = (long)(seconds * kSampleRate);
    for(long i = 0; i < samples; i++)
    {
        if(e.Process())
            ticks++;
    }
    return ticks;
}

TEST(no_ticks_while_stopped)
{
    Engine e;
    e.Init(kSampleRate);
    CHECK_EQ(RunFor(e, 1.0f), 0);
}

TEST(tick_rate_120bpm)
{
    Engine e;
    e.Init(kSampleRate);
    e.SetBpm(120);
    e.Play();
    // 120 BPM * 96 PPQN = 11520 ticks/minute = 192 ticks/sec
    long ticks = RunFor(e, 10.0f);
    CHECK_NEAR(ticks, 1920, 2);
}

TEST(tick_rate_60bpm)
{
    Engine e;
    e.Init(kSampleRate);
    e.SetBpm(60);
    e.Play();
    // 60 * 96 / 60 = 96 ticks/sec
    long ticks = RunFor(e, 10.0f);
    CHECK_NEAR(ticks, 960, 2);
}

TEST(tick_rate_200bpm)
{
    Engine e;
    e.Init(kSampleRate);
    e.SetBpm(200);
    e.Play();
    // 200 * 96 / 60 = 320 ticks/sec
    long ticks = RunFor(e, 10.0f);
    CHECK_NEAR(ticks, 3200, 2);
}

TEST(accumulator_stability_over_a_minute)
{
    Engine e;
    e.Init(kSampleRate);
    e.SetBpm(120);
    e.Play();
    // One full minute: 11520 expected ticks. Float accumulator drift
    // must stay within a tick.
    long ticks = RunFor(e, 60.0f);
    CHECK_NEAR(ticks, 11520, 2);
}

TEST(position_wraps_at_pattern_length)
{
    Engine e;
    e.Init(kSampleRate);
    e.SetBpm(240); // fast to cover the pattern quickly
    e.Play();
    // Default pattern = 4 bars = 1536 ticks. Run long enough to wrap.
    RunFor(e, 60.0f);
    CHECK(e.GetPosition().tick < e.GetPatternTicks());
}

TEST(bar_beat_pulse_derivation)
{
    Position p;
    p.tick = 0;
    p.UpdateFromTick();
    CHECK_EQ(p.bar, 1);
    CHECK_EQ(p.beat, 1);
    CHECK_EQ(p.pulse, 0);

    p.tick = TICKS_PER_BAR + PPQN * 2 + 5; // bar 2, beat 3, pulse 5
    p.UpdateFromTick();
    CHECK_EQ(p.bar, 2);
    CHECK_EQ(p.beat, 3);
    CHECK_EQ(p.pulse, 5);
}

TEST(bpm_clamping)
{
    Engine e;
    e.Init(kSampleRate);
    e.SetBpm(10);
    CHECK_EQ(e.GetBpm(), MIN_BPM);
    e.SetBpm(999);
    CHECK_EQ(e.GetBpm(), MAX_BPM);
}

TEST(stop_preserves_position_reset_clears_it)
{
    Engine e;
    e.Init(kSampleRate);
    e.SetBpm(120);
    e.Play();
    RunFor(e, 1.0f);
    CHECK(e.GetPosition().tick > 0);

    e.Stop();
    uint32_t at_stop = e.GetPosition().tick;
    RunFor(e, 1.0f); // stopped: no movement
    CHECK_EQ(e.GetPosition().tick, at_stop);

    e.StopAndReset();
    CHECK_EQ(e.GetPosition().tick, 0);
}
