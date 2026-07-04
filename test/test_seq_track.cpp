#include "mini_test.h"
#include "seq_track.h"

#include <vector>

using namespace Track;

// Dispatch collector (DispatchFn is a plain function pointer)
struct Dispatched
{
    uint32_t tick;
    uint8_t  status, d1, d2;
    float    scale;
};
static std::vector<Dispatched> g_out;
static uint32_t                g_now = 0;

static void Collect(uint8_t status, uint8_t d1, uint8_t d2, float scale)
{
    g_out.push_back({g_now, status, d1, d2, scale});
}

static void RunTicks(Registry& reg, Mixer::Engine& mixer, uint32_t from,
                     uint32_t to_exclusive)
{
    for(uint32_t t = from; t < to_exclusive; t++)
    {
        g_now = t;
        SeqTrack::ProcessTick(reg, mixer, t, Collect);
    }
}

static int MakeTrack(Registry& reg, Kind kind, uint8_t bars,
                     std::initializer_list<MidiEv> evs)
{
    int slot = reg.Create(kind, bars);
    Slot& s  = reg.Get(slot);
    for(const auto& e : evs)
    {
        s.events[s.event_count++] = e;
    }
    reg.Activate(slot);
    return slot;
}

TEST(seq_plays_and_loops_one_bar_track)
{
    Registry reg;
    reg.Init();
    Mixer::Engine mixer;
    mixer.Init();
    g_out.clear();

    MakeTrack(reg, Kind::MidiDrum, 1,
              {{0, 0x99, 36, 127}, {96, 0x99, 38, 100}});

    RunTicks(reg, mixer, 0, 768); // two loop cycles
    CHECK_EQ((int)g_out.size(), 4);
    CHECK_EQ(g_out[0].tick, 0u);
    CHECK_EQ(g_out[1].tick, 96u);
    CHECK_EQ(g_out[2].tick, 384u); // second cycle
    CHECK_EQ(g_out[3].tick, 480u);
    CHECK_EQ(g_out[2].d1, 36);
}

TEST(seq_polymeter_alignment)
{
    Registry reg;
    reg.Init();
    Mixer::Engine mixer;
    mixer.Init();
    g_out.clear();

    // 1-bar hat under a 2-bar bass: both must stay phase-locked
    MakeTrack(reg, Kind::MidiDrum, 1, {{0, 0x99, 42, 100}});
    MakeTrack(reg, Kind::MidiSynth, 2,
              {{0, 0x90, 40, 100}, {380, 0x80, 40, 0}});

    RunTicks(reg, mixer, 0, 1536); // 4 bars = 4 hat cycles, 2 bass cycles
    int hats = 0, bass_on = 0;
    for(auto& d : g_out)
    {
        if(d.d1 == 42)
        {
            CHECK_EQ(d.tick % 384, 0u); // hat always on the bar
            hats++;
        }
        if(d.d1 == 40 && d.status == 0x90)
        {
            CHECK_EQ(d.tick % 768, 0u); // bass on its own 2-bar grid
            bass_on++;
        }
    }
    CHECK_EQ(hats, 4);
    CHECK_EQ(bass_on, 2);
}

TEST(seq_velocity_scaled_by_strip_gain)
{
    Registry reg;
    reg.Init();
    Mixer::Engine mixer;
    mixer.Init();
    g_out.clear();

    int slot = MakeTrack(reg, Kind::MidiDrum, 1, {{0, 0x99, 36, 127}});
    mixer.Get(slot).gain = 0.5f;

    RunTicks(reg, mixer, 0, 1);
    CHECK_EQ((int)g_out.size(), 1);
    CHECK_NEAR(g_out[0].scale, 0.5f, 0.001);
}

TEST(seq_mute_suppresses_and_releases_notes)
{
    Registry reg;
    reg.Init();
    Mixer::Engine mixer;
    mixer.Init();
    g_out.clear();

    // Synth note on at 0, off at 300
    int slot = MakeTrack(reg, Kind::MidiSynth, 1,
                         {{0, 0x90, 60, 100}, {300, 0x80, 60, 0}});

    RunTicks(reg, mixer, 0, 100); // note is sounding
    size_t before = g_out.size();
    CHECK_EQ((int)before, 1); // just the note-on

    mixer.Get(slot).mute = true;
    RunTicks(reg, mixer, 100, 200);
    // Mute transition must release the sounding note exactly once
    CHECK_EQ((int)g_out.size(), 2);
    CHECK_EQ(g_out[1].status, 0x80);
    CHECK_EQ(g_out[1].d1, 60);

    // While muted: nothing plays, even across the loop seam
    RunTicks(reg, mixer, 200, 768);
    CHECK_EQ((int)g_out.size(), 2);

    // Unmute: playback resumes at the right position next cycle
    mixer.Get(slot).mute = false;
    RunTicks(reg, mixer, 768, 1152);
    bool saw_on = false;
    for(size_t i = 2; i < g_out.size(); i++)
    {
        if(g_out[i].status == 0x90 && g_out[i].tick == 768)
            saw_on = true;
    }
    CHECK(saw_on);
}

TEST(seq_destroy_mid_note_releases)
{
    Registry reg;
    reg.Init();
    Mixer::Engine mixer;
    mixer.Init();
    g_out.clear();

    int slot = MakeTrack(reg, Kind::MidiSynth, 1,
                         {{0, 0x90, 72, 100}, {300, 0x80, 72, 0}});

    RunTicks(reg, mixer, 0, 50); // note sounding
    reg.Destroy(slot);           // undo mid-note
    RunTicks(reg, mixer, 50, 60);

    CHECK_EQ((int)g_out.size(), 2);
    CHECK_EQ(g_out[1].status, 0x80);
    CHECK_EQ(g_out[1].d1, 72);
}

TEST(seq_events_mid_capture_not_played_until_activate)
{
    Registry reg;
    reg.Init();
    Mixer::Engine mixer;
    mixer.Init();
    g_out.clear();

    int   slot = reg.Create(Kind::MidiDrum, 1);
    Slot& s    = reg.Get(slot);
    s.events[s.event_count++] = {0, 0x99, 36, 127};

    RunTicks(reg, mixer, 0, 384); // reserved but not active: silent
    CHECK_EQ((int)g_out.size(), 0);

    reg.Activate(slot);
    // Position-0 event fires at every bar boundary from activation on:
    // global ticks 384 and 768 in this range
    RunTicks(reg, mixer, 384, 769);
    CHECK_EQ((int)g_out.size(), 2);
    CHECK_EQ(g_out[0].tick, 384u);
    CHECK_EQ(g_out[1].tick, 768u);
}
