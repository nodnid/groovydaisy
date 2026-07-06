#include "mini_test.h"
#include "groove.h"

using namespace Groove;
using Track::MidiEv;

// ---------------------------------------------------------------------------
// Quantize
// ---------------------------------------------------------------------------

TEST(groove_quant_delta_modes)
{
    // tick 30 -> nearest 16th is 24 (delta -6); tick 37 -> 36... no, grid
    // is 24s: 37 -> nearest is 48 (delta +11)? |37-24|=13, |37-48|=11 -> 48
    CHECK_EQ(QuantDelta(30, Quant::Off), 0);
    CHECK_EQ(QuantDelta(30, Quant::Hard), -6);
    CHECK_EQ(QuantDelta(30, Quant::Light), -3);
    CHECK_EQ(QuantDelta(37, Quant::Hard), 11);
    CHECK_EQ(QuantDelta(24, Quant::Hard), 0);   // already on the grid
    CHECK_EQ(QuantDelta(12, Quant::Hard), 12);  // exact midpoint rounds up
}

TEST(groove_quantize_hard_moves_on_and_off_together)
{
    // Synth note: on at 27 (nearest 24, delta -3), off at 75.
    // The off must travel by the ON's delta (-3) -> 72, not its own.
    MidiEv ev[] = {
        {27, 0x90, 60, 100},
        {75, 0x80, 60, 0},
    };
    QuantizeEvents(ev, 2, 384, Quant::Hard);
    CHECK_EQ(ev[0].tick, 24u);
    CHECK_EQ(ev[0].status, 0x90);
    CHECK_EQ(ev[1].tick, 72u);
    CHECK_EQ(ev[1].status, 0x80);
}

TEST(groove_quantize_light_halves_the_delta)
{
    MidiEv ev[] = {
        {30, 0x90, 60, 100}, // delta -6 -> light -3
        {50, 0x80, 60, 0},
    };
    QuantizeEvents(ev, 2, 384, Quant::Light);
    CHECK_EQ(ev[0].tick, 27u);
    CHECK_EQ(ev[1].tick, 47u);
}

TEST(groove_quantize_wraps_the_late_downbeat_to_zero)
{
    // A hit meant for the downbeat, played 4 ticks early... no — played
    // LATE relative to the previous bar: tick 380 of a 384 loop rounds to
    // 384 == 0. Its off (synthesized at 383 or landing early next pass)
    // wraps with it.
    MidiEv ev[] = {
        {2, 0x80, 60, 0},    // wrapped off (on comes later in the array)
        {380, 0x90, 60, 100},
    };
    QuantizeEvents(ev, 2, 384, Quant::Hard);
    // on: 380 + 4 -> 384 -> wraps to 0; orphan off travels +4 -> 6
    bool found_on_at_0 = false, found_off_at_6 = false;
    for(const auto& e : ev)
    {
        if(e.status == 0x90 && e.tick == 0)
            found_on_at_0 = true;
        if(e.status == 0x80 && e.tick == 6)
            found_off_at_6 = true;
    }
    CHECK(found_on_at_0);
    CHECK(found_off_at_6);
}

TEST(groove_quantize_resorts_events)
{
    // Hard-quantizing can reorder: on at 13 snaps to 24, passing an
    // event at 20. Output must be tick-sorted again.
    MidiEv ev[] = {
        {13, 0x99, 36, 100}, // drum hit -> 24
        {20, 0x99, 38, 100}, // drum hit -> 24
        {30, 0x99, 40, 100}, // -> 24
    };
    QuantizeEvents(ev, 3, 384, Quant::Hard);
    for(int i = 1; i < 3; i++)
    {
        CHECK(ev[i].tick >= ev[i - 1].tick);
    }
    CHECK_EQ(ev[0].tick, 24u);
    CHECK_EQ(ev[2].tick, 24u);
}

TEST(groove_quantize_leaves_cc_events_alone)
{
    MidiEv ev[] = {
        {13, 0xB0, 74, 90}, // CC sweep point: never quantized
        {27, 0x90, 60, 100},
        {40, 0x80, 60, 0},
    };
    QuantizeEvents(ev, 3, 384, Quant::Hard);
    bool found_cc_at_13 = false;
    for(const auto& e : ev)
    {
        if((e.status & 0xF0) == 0xB0)
        {
            CHECK_EQ(e.tick, 13u);
            found_cc_at_13 = true;
        }
    }
    CHECK(found_cc_at_13);
}

TEST(groove_quantize_off_is_identity)
{
    MidiEv ev[] = {
        {13, 0x90, 60, 100},
        {40, 0x80, 60, 0},
    };
    QuantizeEvents(ev, 2, 384, Quant::Off);
    CHECK_EQ(ev[0].tick, 13u);
    CHECK_EQ(ev[1].tick, 40u);
}

TEST(groove_quantize_retrigger_pairs_by_recency)
{
    // Same note twice: each off follows its own on's delta.
    MidiEv ev[] = {
        {27, 0x90, 60, 100}, // delta -3
        {45, 0x80, 60, 0},   // travels -3 -> 42
        {58, 0x90, 60, 100}, // delta -10 -> 48
        {90, 0x80, 60, 0},   // travels -10 -> 80
    };
    QuantizeEvents(ev, 4, 384, Quant::Hard);
    CHECK_EQ(ev[0].tick, 24u);
    CHECK_EQ(ev[1].tick, 42u);
    CHECK_EQ(ev[2].tick, 48u);
    CHECK_EQ(ev[3].tick, 80u);
}

// ---------------------------------------------------------------------------
// Swing
// ---------------------------------------------------------------------------

TEST(groove_swing_pct_conversion)
{
    CHECK_EQ(SwingPctToTicks(50), 0);
    CHECK_EQ(SwingPctToTicks(75), 12);
    CHECK_EQ(SwingPctToTicks(62), 6); // (12*48+50)/100 = 5.8 -> 6
    CHECK_EQ(SwingPctToTicks(40), 0);  // clamped
    CHECK_EQ(SwingPctToTicks(99), 12); // clamped
    CHECK_EQ(SwingTicksToPct(0), 50);
    CHECK_EQ(SwingTicksToPct(12), 75);
}

TEST(groove_swing_zero_is_identity)
{
    for(uint32_t t = 0; t < 384; t++)
    {
        CHECK_EQ(SwingWarp(t, 0), t);
    }
}

TEST(groove_swing_delays_the_offbeat_sixteenth)
{
    // Full shuffle: the offbeat 16th (24) lands at 36; cell edges fixed
    CHECK_EQ(SwingWarp(0, 12), 0u);
    CHECK_EQ(SwingWarp(24, 12), 36u);
    CHECK_EQ(SwingWarp(48, 12), 48u);   // next 8th: fixed point
    CHECK_EQ(SwingWarp(96, 12), 96u);   // beat: fixed point
    CHECK_EQ(SwingWarp(384, 12), 384u); // bar: fixed point
    // Half shuffle
    CHECK_EQ(SwingWarp(24, 6), 30u);
}

TEST(groove_swing_is_monotonic_and_stays_in_the_cell)
{
    for(uint8_t s = 0; s <= MAX_SWING_TICKS; s++)
    {
        uint32_t prev = 0;
        for(uint32_t t = 0; t < 768; t++)
        {
            uint32_t w = SwingWarp(t, s);
            if(t > 0)
            {
                CHECK(w >= prev); // monotonic: sorted arrays stay sorted
            }
            CHECK(w >= t);                      // never early
            CHECK(w / CELL == t / CELL);        // never leaves its 8th
            prev = w;
        }
    }
}

// ---------------------------------------------------------------------------
// Velocity compression
// ---------------------------------------------------------------------------

TEST(groove_vel_compress_lifts_soft_keeps_loud)
{
    CHECK_EQ(CompressVelocity(0), 0);     // a rest stays a rest
    CHECK_EQ(CompressVelocity(127), 127); // full stays full
    CHECK(CompressVelocity(32) > 32);     // soft comes up
    CHECK(CompressVelocity(1) >= 1);
    // Monotonic: relative dynamics preserved
    for(int v = 1; v < 127; v++)
    {
        CHECK(CompressVelocity((uint8_t)(v + 1)) >= CompressVelocity((uint8_t)v));
    }
}

TEST(groove_vel_compress_touches_only_note_ons)
{
    MidiEv ev[] = {
        {0, 0x99, 36, 40},
        {10, 0x89, 36, 0},  // note-off: untouched
        {20, 0xB0, 74, 40}, // CC: untouched
    };
    CompressVelocities(ev, 3);
    CHECK(ev[0].d2 > 40);
    CHECK_EQ(ev[1].d2, 0);
    CHECK_EQ(ev[2].d2, 40);
}

// ---------------------------------------------------------------------------
// CC automation helpers
// ---------------------------------------------------------------------------

TEST(groove_auto_cc_table_round_trips)
{
    for(int i = 0; i < Track::MAX_AUTO_CC; i++)
    {
        CHECK_EQ(AutoCcIndex(AUTO_CCS[i]), i);
    }
    CHECK_EQ(AutoCcIndex(3), -1);  // Live button CC is not automatable
    CHECK_EQ(AutoCcIndex(1), -1);  // bank-switch/mod collision stays out
}

TEST(groove_blend_cc_rides_and_clamps)
{
    CHECK_EQ(BlendCc(60, 64, 64), 60);   // knob untouched since commit
    CHECK_EQ(BlendCc(60, 84, 64), 80);   // +20 rides on top
    CHECK_EQ(BlendCc(60, 24, 64), 20);   // -40 rides down
    CHECK_EQ(BlendCc(120, 127, 64), 127); // clamped high
    CHECK_EQ(BlendCc(10, 0, 64), 0);      // clamped low
}

TEST(groove_thinner_min_interval_and_delta)
{
    CcThinner th;
    th.Reset();
    CHECK(th.ShouldRecord(0, 100, 64));  // first point always lands
    CHECK(!th.ShouldRecord(0, 103, 90)); // too soon (interval 6)
    CHECK(!th.ShouldRecord(0, 110, 65)); // too small (delta 2)
    CHECK(th.ShouldRecord(0, 110, 70));  // spaced and moved: record
    CHECK(th.ShouldRecord(0, 116, 72));
    // Independent per CC index
    CHECK(th.ShouldRecord(5, 116, 10));
    th.Reset();
    CHECK(th.ShouldRecord(0, 116, 72)); // reset forgets history
}
