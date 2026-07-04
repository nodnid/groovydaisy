#include "mini_test.h"
#include "track.h"

using namespace Track;

TEST(registry_create_activate_destroy)
{
    Registry reg;
    reg.Init();

    int slot = reg.Create(Kind::MidiDrum, 2);
    CHECK(slot >= 0);
    CHECK(!reg.Get(slot).active.load());
    CHECK_EQ(reg.CountActive(), 0);

    reg.Activate(slot);
    CHECK(reg.Get(slot).active.load());
    CHECK_EQ(reg.CountActive(), 1);
    CHECK_EQ((int)reg.Get(slot).LengthTicks(), 768);

    uint8_t gen_before = reg.Get(slot).gen;
    reg.Destroy(slot);
    CHECK(!reg.Get(slot).active.load());
    CHECK_EQ(reg.Get(slot).gen, gen_before + 1);
    CHECK_EQ(reg.CountActive(), 0);
}

TEST(registry_kind_caps)
{
    Registry reg;
    reg.Init();

    // Fill 16 audio slots; the 17th must refuse
    for(int i = 0; i < MAX_PER_KIND; i++)
    {
        int s = reg.Create(Kind::Audio, 1);
        CHECK(s >= 0);
        reg.Activate(s);
    }
    CHECK_EQ(reg.Create(Kind::Audio, 1), -1);

    // But MIDI kinds still fit
    int s = reg.Create(Kind::MidiSynth, 4);
    CHECK(s >= 0);
}

TEST(registry_newest_after_out_of_order_deletes)
{
    Registry reg;
    reg.Init();

    int a = reg.Create(Kind::MidiDrum, 1);
    reg.Activate(a);
    int b = reg.Create(Kind::MidiSynth, 2);
    reg.Activate(b);
    int c = reg.Create(Kind::MidiDrum, 4);
    reg.Activate(c);

    CHECK_EQ(reg.NewestActive(), c);
    reg.Destroy(b); // delete the MIDDLE track
    CHECK_EQ(reg.NewestActive(), c);
    reg.Destroy(c); // undo
    CHECK_EQ(reg.NewestActive(), a);
    reg.Destroy(a);
    CHECK_EQ(reg.NewestActive(), -1);
}

TEST(registry_slot_reuse_bumps_gen)
{
    Registry reg;
    reg.Init();

    int a = reg.Create(Kind::MidiDrum, 1);
    reg.Activate(a);
    uint8_t g1 = reg.Get(a).gen;
    reg.Destroy(a);

    int b = reg.Create(Kind::MidiSynth, 1);
    // Same slot should be reusable, with a different gen
    CHECK_EQ(b, a);
    CHECK(reg.Get(b).gen != g1);
}

TEST(registry_reserved_slot_not_double_allocated)
{
    Registry reg;
    reg.Init();

    int a = reg.Create(Kind::MidiDrum, 1); // reserved, not yet active
    int b = reg.Create(Kind::MidiDrum, 1);
    CHECK(a != b);
    reg.Abort(a); // capture failed: slot returns to the pool
    int c = reg.Create(Kind::MidiDrum, 1);
    CHECK_EQ(c, a);
}
