#include "mini_test.h"
#include "ui_link.h"
#include "protocol.h"

#include <cstring>

// Capture buffer for golden-byte checks
static uint8_t captured[Protocol::MAX_MESSAGE];
static size_t  captured_len = 0;

static void CaptureSend(const uint8_t* data, size_t len)
{
    memcpy(captured, data, len);
    captured_len = len;
}

static UiLink::Publisher MakePublisher()
{
    UiLink::Publisher p;
    p.Init(CaptureSend);
    captured_len = 0;
    return p;
}

TEST(ui_link_transport_golden_bytes)
{
    auto pub = MakePublisher();
    pub.Transport(true, false, 120.0f, false);

    // [AA][02][05 00][playing=1][locked=0][bpm_x10=1200 LE][preroll=0][csum]
    CHECK_EQ(captured_len, 10);
    CHECK_EQ(captured[0], 0xAA);
    CHECK_EQ(captured[1], Protocol::MSG_TRANSPORT);
    CHECK_EQ(captured[2], 5); // len lo
    CHECK_EQ(captured[3], 0); // len hi
    CHECK_EQ(captured[4], 1); // playing
    CHECK_EQ(captured[5], 0); // locked
    CHECK_EQ(captured[6], 1200 & 0xFF);
    CHECK_EQ(captured[7], 1200 >> 8);
    CHECK_EQ(captured[8], 0); // preroll

    // Frame must parse back cleanly
    Protocol::Parser parser;
    parser.Reset();
    int complete = 0;
    for(size_t i = 0; i < captured_len; i++)
        if(parser.Feed(captured[i]))
            complete++;
    CHECK_EQ(complete, 1);
    CHECK_EQ(parser.type, Protocol::MSG_TRANSPORT);
}

TEST(ui_link_transport_fractional_bpm)
{
    auto pub = MakePublisher();
    pub.Transport(false, true, 93.7f, true);
    uint16_t bpm_x10 = captured[6] | (captured[7] << 8);
    CHECK_EQ(bpm_x10, 937);
    CHECK_EQ(captured[4], 0); // stopped
    CHECK_EQ(captured[5], 1); // locked
    CHECK_EQ(captured[8], 1); // preroll
}

TEST(ui_link_sync_tick_le)
{
    auto pub = MakePublisher();
    pub.Sync(0x0A0B0C0Du);
    CHECK_EQ(captured[1], Protocol::MSG_SYNC);
    CHECK_EQ(captured[4], 0x0D);
    CHECK_EQ(captured[5], 0x0C);
    CHECK_EQ(captured[6], 0x0B);
    CHECK_EQ(captured[7], 0x0A);
}

TEST(ui_link_hello_carries_proto_ver)
{
    auto pub = MakePublisher();
    pub.Hello();
    CHECK_EQ(captured[1], Protocol::MSG_HELLO);
    CHECK_EQ(captured[4], Protocol::PROTO_VER);
}

TEST(ui_link_mixer_strip_layout)
{
    auto pub = MakePublisher();
    pub.MixerStrip(33, 101, 64, true, 12, 34);
    CHECK_EQ(captured[1], Protocol::MSG_MIXER);
    CHECK_EQ(captured[2], 7); // payload length (7th byte = input gain)
    CHECK_EQ(captured[4], 33);
    CHECK_EQ(captured[5], 101);
    CHECK_EQ(captured[6], 64);
    CHECK_EQ(captured[7], 1);
    CHECK_EQ(captured[8], 12);
    CHECK_EQ(captured[9], 34);
    CHECK_EQ(captured[10], 0); // non-guitar strips carry 0

    pub.MixerStrip(32, 101, 64, false, 12, 34, 84);
    CHECK_EQ(captured[10], 84); // guitar preamp cc rides along
}

TEST(ui_link_debug_truncates_to_max_payload)
{
    auto pub = MakePublisher();
    char big[600];
    memset(big, 'x', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    pub.Debug(big);
    uint16_t len = captured[2] | (captured[3] << 8);
    CHECK_EQ(len, Protocol::MAX_PAYLOAD);
    CHECK_EQ(captured_len, 5 + Protocol::MAX_PAYLOAD);
}

TEST(ui_link_stats_golden_bytes)
{
    auto pub = MakePublisher();
    pub.Stats(1, 0x0203, 0, 0x04050607);

    // [AA][23][10 00][4x u32 LE][csum]
    CHECK_EQ(captured_len, 21);
    CHECK_EQ(captured[0], 0xAA);
    CHECK_EQ(captured[1], Protocol::MSG_STATS);
    CHECK_EQ(captured[2], 16);
    CHECK_EQ(captured[3], 0);
    CHECK_EQ(captured[4], 1); // midi_drops LE
    CHECK_EQ(captured[8], 0x03); // cc_drops LE lo
    CHECK_EQ(captured[9], 0x02);
    CHECK_EQ(captured[16], 0x07); // tx_lapped_ext LE
    CHECK_EQ(captured[19], 0x04);

    Protocol::Parser parser;
    parser.Reset();
    int complete = 0;
    for(size_t i = 0; i < captured_len; i++)
    {
        if(parser.Feed(captured[i]))
            complete++;
    }
    CHECK_EQ(complete, 1);
}
