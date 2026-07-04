#include "mini_test.h"
#include "protocol.h"

#include <cstring>

using namespace Protocol;

// Feed a buffer through a parser, counting complete messages.
static int FeedAll(Parser& p, const uint8_t* buf, size_t len)
{
    int complete = 0;
    for(size_t i = 0; i < len; i++)
    {
        if(p.Feed(buf[i]))
            complete++;
    }
    return complete;
}

TEST(build_then_parse_roundtrip)
{
    uint8_t payload[] = {0x10, 0x20, 0x30, 0x40};
    uint8_t buf[MAX_MESSAGE];
    size_t  len = BuildMessage(buf, MSG_MIDI_IN, payload, sizeof(payload));
    CHECK_EQ(len, 5 + sizeof(payload));

    Parser p;
    p.Reset();
    CHECK_EQ(FeedAll(p, buf, len), 1);
    CHECK_EQ(p.type, MSG_MIDI_IN);
    CHECK_EQ(p.payload_len, sizeof(payload));
    CHECK(memcmp(p.payload, payload, sizeof(payload)) == 0);
}

TEST(zero_length_payload)
{
    uint8_t buf[MAX_MESSAGE];
    size_t  len = BuildMessage(buf, CMD_PLAY, nullptr, 0);
    CHECK_EQ(len, 5);

    Parser p;
    p.Reset();
    CHECK_EQ(FeedAll(p, buf, len), 1);
    CHECK_EQ(p.type, CMD_PLAY);
    CHECK_EQ(p.payload_len, 0);
}

TEST(max_payload_roundtrip)
{
    uint8_t payload[MAX_PAYLOAD];
    for(size_t i = 0; i < MAX_PAYLOAD; i++)
        payload[i] = (uint8_t)(i * 7);

    uint8_t buf[MAX_MESSAGE];
    size_t  len = BuildMessage(buf, MSG_DEBUG, payload, MAX_PAYLOAD);

    Parser p;
    p.Reset();
    CHECK_EQ(FeedAll(p, buf, len), 1);
    CHECK_EQ(p.payload_len, MAX_PAYLOAD);
    CHECK(memcmp(p.payload, payload, MAX_PAYLOAD) == 0);
}

TEST(corrupted_checksum_rejected)
{
    uint8_t payload[] = {1, 2, 3};
    uint8_t buf[MAX_MESSAGE];
    size_t  len = BuildMessage(buf, MSG_TICK, payload, sizeof(payload));
    buf[len - 1] ^= 0xFF; // corrupt checksum

    Parser p;
    p.Reset();
    CHECK_EQ(FeedAll(p, buf, len), 0);
}

TEST(corrupted_payload_rejected)
{
    uint8_t payload[] = {1, 2, 3};
    uint8_t buf[MAX_MESSAGE];
    size_t  len = BuildMessage(buf, MSG_TICK, payload, sizeof(payload));
    buf[5] ^= 0x55; // corrupt a payload byte; checksum now mismatches

    Parser p;
    p.Reset();
    CHECK_EQ(FeedAll(p, buf, len), 0);
}

TEST(oversized_length_resets_parser)
{
    // Hand-build a frame claiming payload_len = MAX_PAYLOAD + 1
    uint8_t  bad[4];
    uint16_t bad_len = MAX_PAYLOAD + 1;
    bad[0]           = SYNC_BYTE;
    bad[1]           = MSG_DEBUG;
    bad[2]           = bad_len & 0xFF;
    bad[3]           = (bad_len >> 8) & 0xFF;

    Parser p;
    p.Reset();
    CHECK_EQ(FeedAll(p, bad, sizeof(bad)), 0);
    // Parser must be back at WAIT_SYNC: a valid message parses next.
    uint8_t buf[MAX_MESSAGE];
    size_t  len = BuildMessage(buf, CMD_STOP, nullptr, 0);
    CHECK_EQ(FeedAll(p, buf, len), 1);
    CHECK_EQ(p.type, CMD_STOP);
}

TEST(resync_after_garbage)
{
    uint8_t garbage[] = {0x00, 0x13, 0x37, 0xFE, 0x42};
    uint8_t buf[MAX_MESSAGE];
    size_t  len = BuildMessage(buf, MSG_VOICES, nullptr, 0);

    Parser p;
    p.Reset();
    CHECK_EQ(FeedAll(p, garbage, sizeof(garbage)), 0);
    CHECK_EQ(FeedAll(p, buf, len), 1);
    CHECK_EQ(p.type, MSG_VOICES);
}

TEST(back_to_back_messages)
{
    uint8_t a[MAX_MESSAGE], b[MAX_MESSAGE];
    uint8_t pa[] = {0xAA, 0xBB}; // payload containing the sync byte value
    size_t  la   = BuildMessage(a, MSG_TICK, pa, sizeof(pa));
    size_t  lb   = BuildMessage(b, MSG_TRANSPORT, nullptr, 0);

    uint8_t stream[2 * MAX_MESSAGE];
    memcpy(stream, a, la);
    memcpy(stream + la, b, lb);

    Parser p;
    p.Reset();
    CHECK_EQ(FeedAll(p, stream, la + lb), 2);
    CHECK_EQ(p.type, MSG_TRANSPORT); // last message parsed
}

TEST(truncated_message_then_new_frame)
{
    uint8_t payload[] = {9, 8, 7, 6};
    uint8_t buf[MAX_MESSAGE];
    size_t  len = BuildMessage(buf, MSG_MIDI_IN, payload, sizeof(payload));

    Parser p;
    p.Reset();
    // Feed only half the frame (simulates a dropped connection mid-frame)
    CHECK_EQ(FeedAll(p, buf, len / 2), 0);

    // A fresh frame arrives. Its leading SYNC lands mid-payload of the
    // truncated frame, so the parser may consume up to one frame's worth
    // of bytes before recovering — but it must recover: feed the new
    // frame twice and require at least one complete parse.
    int got = FeedAll(p, buf, len);
    got += FeedAll(p, buf, len);
    CHECK(got >= 1);
}
