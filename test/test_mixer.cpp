#include "mini_test.h"
#include "mixer.h"

using namespace Mixer;

TEST(mixer_sends_are_post_fader)
{
    Engine m;
    m.Init();
    Strip& s   = m.Get(0);
    s.gain     = 0.5f;
    s.send_rev = 1.0f;
    s.send_dly = 0.5f;

    Bus bus;
    bus.Clear();
    m.AddMono(0, 1.0f, bus);

    CHECK_NEAR(bus.l, 0.5f, 1e-6);
    CHECK_NEAR(bus.r, 0.5f, 1e-6);
    CHECK_NEAR(bus.rev, 0.5f, 1e-6);  // post-fader: gain applied first
    CHECK_NEAR(bus.dly, 0.25f, 1e-6);
}

TEST(mixer_mute_kills_sends_too)
{
    Engine m;
    m.Init();
    Strip& s   = m.Get(3);
    s.send_rev = 1.0f;
    s.mute     = true;

    Bus bus;
    bus.Clear();
    m.AddMono(3, 1.0f, bus);
    CHECK_NEAR(bus.l, 0.0f, 1e-9);
    CHECK_NEAR(bus.rev, 0.0f, 1e-9); // a muted strip feeds no reverb tail
}

TEST(mixer_stereo_send_is_mono_sum)
{
    Engine m;
    m.Init();
    Strip& s   = m.Get(STRIP_SYNTH);
    s.gain     = 1.0f;
    s.send_rev = 1.0f;

    Bus bus;
    bus.Clear();
    m.AddStereo(STRIP_SYNTH, 0.8f, 0.4f, bus);
    CHECK_NEAR(bus.rev, 0.6f, 1e-6); // (L+R)/2
}

TEST(mixer_peaks_accumulate_and_clear_on_read)
{
    Engine m;
    m.Init();
    m.Get(0).gain = 1.0f;

    Bus bus;
    bus.Clear();
    m.AddMono(0, 0.3f, bus);
    m.AddMono(0, -0.7f, bus); // negative excursion counts
    m.AddMono(0, 0.5f, bus);

    CHECK_NEAR(m.ReadPeak(0), 0.7f, 1e-6);
    CHECK_NEAR(m.ReadPeak(0), 0.0f, 1e-9); // read clears

    bus.Clear();
    m.AddMono(0, 0.2f, bus);
    m.ApplyMaster(bus.l, bus.r);
    float pl, pr;
    m.ReadMasterPeaks(pl, pr);
    CHECK_NEAR(pl, 0.2f * 0.85f, 1e-6); // default master 0.85
    m.ReadMasterPeaks(pl, pr);
    CHECK_NEAR(pl, 0.0f, 1e-9);
}

TEST(mixer_peak_to_cc_taper)
{
    CHECK_EQ(PeakToCc(0.0f), 0);
    CHECK_EQ(PeakToCc(1.0f), 127);
    CHECK_EQ(PeakToCc(2.0f), 127);       // clamped, no wrap on clipping
    CHECK(PeakToCc(0.01f) >= 12);        // sqrt taper: -40 dB still visible
    CHECK(PeakToCc(0.25f) > PeakToCc(0.1f)); // monotonic
}
