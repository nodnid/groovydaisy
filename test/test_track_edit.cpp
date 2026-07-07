#include "mini_test.h"
#include "track_edit.h"

using namespace TrackEdit;
using Track::MidiEv;

TEST(edit_toggle_drum_adds_and_removes)
{
    MidiEv   ev[16] = {{0, 0x99, 36, 100}, {96, 0x99, 38, 100}};
    uint16_t n      = 2;

    // Add a hat at tick 48 — lands sorted between the two
    CHECK_EQ((int)ToggleDrum(ev, n, 16, 48, 42, 90), (int)Result::Added);
    CHECK_EQ(n, 3);
    CHECK_EQ(ev[1].tick, 48u);
    CHECK_EQ(ev[1].d1, 42);
    CHECK_EQ(ev[1].d2, 90);

    // Same cell again removes it
    CHECK_EQ((int)ToggleDrum(ev, n, 16, 48, 42, 90), (int)Result::Removed);
    CHECK_EQ(n, 2);
    CHECK_EQ(ev[1].tick, 96u);
}

TEST(edit_toggle_drum_respects_capacity)
{
    MidiEv   ev[2] = {{0, 0x99, 36, 100}, {96, 0x99, 38, 100}};
    uint16_t n     = 2;
    CHECK_EQ((int)ToggleDrum(ev, n, 2, 48, 42, 90), (int)Result::Full);
    CHECK_EQ(n, 2);
}

TEST(edit_delete_note_takes_the_off_along)
{
    MidiEv   ev[8] = {{10, 0x90, 60, 100},
                      {20, 0x90, 64, 100},
                      {50, 0x80, 60, 0},
                      {60, 0x80, 64, 0}};
    uint16_t n     = 4;

    CHECK_EQ((int)DeleteNote(ev, n, 10, 60), (int)Result::Removed);
    CHECK_EQ(n, 2);
    CHECK_EQ(ev[0].d1, 64);
    CHECK_EQ(ev[1].d1, 64);

    CHECK_EQ((int)DeleteNote(ev, n, 10, 60), (int)Result::NotFound);
}

TEST(edit_delete_note_wrapped_pair)
{
    // Note sustains across the seam: off at 5 belongs to on at 300
    MidiEv   ev[8] = {{5, 0x80, 60, 0},
                      {100, 0x90, 62, 100},
                      {150, 0x80, 62, 0},
                      {300, 0x90, 60, 100}};
    uint16_t n     = 4;

    CHECK_EQ((int)DeleteNote(ev, n, 300, 60), (int)Result::Removed);
    CHECK_EQ(n, 2);
    for(uint16_t i = 0; i < n; i++)
    {
        CHECK_EQ(ev[i].d1, 62);
    }
}

TEST(edit_move_note_preserves_duration_and_resorts)
{
    MidiEv   ev[8] = {{10, 0x90, 60, 100},
                      {40, 0x80, 60, 0},
                      {200, 0x99, 36, 100}}; // bystander
    uint16_t n     = 3;

    // Drag the note to tick 300, pitch 65
    CHECK_EQ((int)MoveNote(ev, n, 384, 10, 60, 300, 65), (int)Result::Moved);
    CHECK_EQ(n, 3);
    // Sorted: bystander first now
    CHECK_EQ(ev[0].tick, 200u);
    CHECK_EQ(ev[1].tick, 300u);
    CHECK_EQ(ev[1].d1, 65);
    CHECK(IsOn(ev[1]));
    CHECK_EQ(ev[2].tick, 330u); // 30-tick duration preserved
    CHECK_EQ(ev[2].d1, 65);
    CHECK(IsOff(ev[2]));
}

TEST(edit_move_note_off_wraps_at_loop_end)
{
    MidiEv   ev[4] = {{10, 0x90, 60, 100}, {40, 0x80, 60, 0}};
    uint16_t n     = 2;

    // Move on to 370 of a 384 loop: off lands at (370+30) % 384 = 16
    CHECK_EQ((int)MoveNote(ev, n, 384, 10, 60, 370, 60), (int)Result::Moved);
    CHECK_EQ(ev[0].tick, 16u); // wrapped off sorts first
    CHECK(IsOff(ev[0]));
    CHECK_EQ(ev[1].tick, 370u);
    CHECK(IsOn(ev[1]));
}

TEST(edit_move_rejects_out_of_range)
{
    MidiEv   ev[4] = {{10, 0x90, 60, 100}, {40, 0x80, 60, 0}};
    uint16_t n     = 2;
    CHECK_EQ((int)MoveNote(ev, n, 384, 10, 60, 384, 60),
             (int)Result::NotFound);
    CHECK_EQ(ev[0].tick, 10u); // untouched
}

TEST(edit_add_note_inserts_pair_sorted)
{
    MidiEv   ev[8] = {{200, 0x99, 36, 100}};
    uint16_t n     = 1;
    CHECK_EQ((int)AddNote(ev, n, 8, 384, 48, 64, 90, 48), (int)Result::Added);
    CHECK_EQ(n, 3);
    CHECK_EQ(ev[0].tick, 48u);
    CHECK(IsOn(ev[0]));
    CHECK_EQ(ev[1].tick, 96u);
    CHECK(IsOff(ev[1]));

    // off wraps at the seam
    CHECK_EQ((int)AddNote(ev, n, 8, 384, 370, 60, 90, 48), (int)Result::Added);
    bool wrapped = false;
    for(uint16_t i = 0; i < n; i++)
        if(ev[i].d1 == 60 && IsOff(ev[i]) && ev[i].tick == (370u + 48u) % 384u)
            wrapped = true;
    CHECK(wrapped);

    // capacity guard: 5 events + 2 > 6
    uint16_t cap = n;
    CHECK_EQ((int)AddNote(ev, cap, 6, 384, 10, 50, 90, 24), (int)Result::Full);
}
