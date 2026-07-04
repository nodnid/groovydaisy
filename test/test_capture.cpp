#include "mini_test.h"
#include "capture.h"

using namespace Capture;

TEST(window_end_nearest_bar)
{
    CHECK_EQ(WindowEndTick(0), 0);
    CHECK_EQ(WindowEndTick(100), 0);       // early in bar 1: rounds back
    CHECK_EQ(WindowEndTick(191), 0);
    CHECK_EQ(WindowEndTick(192), 384);     // half bar: rounds forward
    CHECK_EQ(WindowEndTick(383), 384);
    CHECK_EQ(WindowEndTick(384), 384);     // exactly on the boundary
    CHECK_EQ(WindowEndTick(384 * 5 + 100), 384 * 5);
    CHECK_EQ(WindowEndTick(384 * 5 + 200), 384 * 6); // ahead -> pending
}

static void PushNote(MidiRing& r, uint32_t tick, uint8_t note, uint8_t vel,
                     bool on = true, uint8_t ch = 0)
{
    r.Push(tick, (on ? 0x90 : 0x80) | ch, note, vel);
}

TEST(extract_basic_window_and_rebase)
{
    MidiRing ring;
    ring.Reset();

    // 1-bar window [384, 768). Note at global 500 -> position 500 % 384 = 116
    PushNote(ring, 100, 60, 100);        // before window: excluded
    PushNote(ring, 500, 62, 90);
    PushNote(ring, 600, 62, 0, true);    // vel-0 note-off for 62
    PushNote(ring, 800, 64, 80);         // after window: excluded

    Track::MidiEv out[512];
    uint16_t      n = 0;
    auto res = ExtractWindow(ring, 768, 1, out, 512, n);
    CHECK_EQ((int)res, (int)ExtractResult::Ok);
    CHECK_EQ(n, 2);
    CHECK_EQ(out[0].tick, 500u % 384u);
    CHECK_EQ(out[0].d1, 62);
    CHECK_EQ(out[1].tick, 600u % 384u);
}

TEST(extract_rebase_preserves_global_phase)
{
    // The whole point of % len rebasing: an event at global tick 384
    // captured into a 4-bar (1536-tick) window ending at 1920 must land
    // at position 384 — so it replays at global ticks 384, 1920, 3456...
    // exactly in phase with where it was played.
    MidiRing ring;
    ring.Reset();
    PushNote(ring, 384, 60, 100);
    PushNote(ring, 400, 60, 0);

    Track::MidiEv out[512];
    uint16_t      n = 0;
    auto res = ExtractWindow(ring, 1920, 4, out, 512, n);
    CHECK_EQ((int)res, (int)ExtractResult::Ok);
    CHECK_EQ(n, 2);
    CHECK_EQ(out[0].tick, 384u);
    CHECK_EQ(out[1].tick, 400u);
}

TEST(extract_synthesizes_dangling_note_off)
{
    MidiRing ring;
    ring.Reset();
    // Note-on inside the window, off never arrives (still holding the key)
    PushNote(ring, 100, 65, 100);

    Track::MidiEv out[512];
    uint16_t      n = 0;
    auto res = ExtractWindow(ring, 384, 1, out, 512, n);
    CHECK_EQ((int)res, (int)ExtractResult::Ok);
    CHECK_EQ(n, 2);
    // Synthesized off at (end-1) % len = 383
    CHECK_EQ(out[1].tick, 383u);
    CHECK_EQ(out[1].status, 0x80);
    CHECK_EQ(out[1].d1, 65);
}

TEST(extract_drops_orphan_note_off)
{
    MidiRing ring;
    ring.Reset();
    // Note-off whose note-on predates the window
    PushNote(ring, 50, 60, 100);       // on, before window
    PushNote(ring, 500, 60, 0);        // off, inside window: orphan
    PushNote(ring, 600, 72, 110);      // a real note inside
    PushNote(ring, 650, 72, 0);

    Track::MidiEv out[512];
    uint16_t      n = 0;
    auto res = ExtractWindow(ring, 768, 1, out, 512, n);
    CHECK_EQ((int)res, (int)ExtractResult::Ok);
    CHECK_EQ(n, 2); // only note 72's pair
    CHECK_EQ(out[0].d1, 72);
    CHECK_EQ(out[1].d1, 72);
}

TEST(extract_empty_window)
{
    MidiRing ring;
    ring.Reset();
    PushNote(ring, 5000, 60, 100); // outside

    Track::MidiEv out[512];
    uint16_t      n = 0;
    auto res = ExtractWindow(ring, 768, 2, out, 512, n);
    CHECK_EQ((int)res, (int)ExtractResult::Empty);
    CHECK_EQ(n, 0);
}

TEST(extract_output_sorted_by_position)
{
    MidiRing ring;
    ring.Reset();
    // 2-bar window [768, 1536); events land at positions that wrap:
    // global 800 -> 800 % 768 = 32; global 1500 -> 1500 % 768 = 732;
    // global 770 -> 2. Recording order != position order after rebase
    // is impossible here (monotonic), but synthesized offs land at 767
    // and drums at various spots — verify the sort invariant anyway.
    PushNote(ring, 770, 36, 100, true, 9);  // drum, pos 2
    PushNote(ring, 800, 60, 100);           // synth on, pos 32
    PushNote(ring, 1500, 38, 90, true, 9);  // drum, pos 732
    // synth 60 never released -> synthesized off at pos 767

    Track::MidiEv out[512];
    uint16_t      n = 0;
    auto res = ExtractWindow(ring, 1536, 2, out, 512, n);
    CHECK_EQ((int)res, (int)ExtractResult::Ok);
    CHECK_EQ(n, 4);
    for(uint16_t i = 1; i < n; i++)
    {
        CHECK(out[i].tick >= out[i - 1].tick);
    }
    CHECK_EQ(out[n - 1].tick, 767u); // dangling off last
}

TEST(extract_drum_channel_needs_no_offs)
{
    MidiRing ring;
    ring.Reset();
    PushNote(ring, 100, 36, 127, true, 9); // kick, no off ever

    Track::MidiEv out[512];
    uint16_t      n = 0;
    auto res = ExtractWindow(ring, 384, 1, out, 512, n);
    CHECK_EQ((int)res, (int)ExtractResult::Ok);
    CHECK_EQ(n, 1); // no synthesized off for drums
}

TEST(ring_wraps_without_losing_recent_history)
{
    MidiRing ring;
    ring.Reset();
    // Overfill the ring: only the newest RING_EVENTS survive
    for(uint32_t i = 0; i < RING_EVENTS + 500; i++)
    {
        PushNote(ring, i * 2, 60, 100, i % 2 == 0);
    }
    // Window over the most recent ticks still extracts fine
    uint32_t last_tick = (RING_EVENTS + 499) * 2;
    uint32_t end       = ((last_tick / 384) + 1) * 384;

    Track::MidiEv out[512];
    uint16_t      n = 0;
    auto res = ExtractWindow(ring, end, 1, out, 512, n);
    CHECK_EQ((int)res, (int)ExtractResult::Ok);
    CHECK(n > 0);
}
