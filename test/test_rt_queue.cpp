#include "mini_test.h"
#include "rt_queue.h"

TEST(spsc_push_pop_ordering)
{
    SpscRing<int, 8> ring;
    for(int i = 0; i < 5; i++)
        CHECK(ring.Push(i));
    int v;
    for(int i = 0; i < 5; i++)
    {
        CHECK(ring.Pop(v));
        CHECK_EQ(v, i);
    }
    CHECK(!ring.Pop(v)); // empty
}

TEST(spsc_full_drops_and_counts)
{
    SpscRing<int, 4> ring;
    for(int i = 0; i < 4; i++)
        CHECK(ring.Push(i));
    CHECK(!ring.Push(99)); // full
    CHECK(!ring.Push(98));
    CHECK_EQ(ring.Dropped(), 2);

    // Dropped items must not corrupt contents
    int v;
    for(int i = 0; i < 4; i++)
    {
        CHECK(ring.Pop(v));
        CHECK_EQ(v, i);
    }
}

TEST(spsc_wraparound)
{
    SpscRing<int, 4> ring;
    int v;
    // Cycle far past the capacity to exercise index wrapping
    for(int i = 0; i < 1000; i++)
    {
        CHECK(ring.Push(i));
        CHECK(ring.Pop(v));
        CHECK_EQ(v, i);
    }
    CHECK_EQ(ring.Dropped(), 0);
    CHECK(ring.Empty());
}

TEST(spsc_interleaved_fill_drain)
{
    SpscRing<uint32_t, 16> ring;
    uint32_t next_push = 0, next_pop = 0, v;
    // Push 3 / pop 2 repeatedly: net fill until full, verify FIFO integrity
    for(int round = 0; round < 100; round++)
    {
        for(int i = 0; i < 3; i++)
            if(ring.Push(next_push))
                next_push++;
        for(int i = 0; i < 2; i++)
            if(ring.Pop(v))
            {
                CHECK_EQ(v, next_pop);
                next_pop++;
            }
    }
    while(ring.Pop(v))
    {
        CHECK_EQ(v, next_pop);
        next_pop++;
    }
    CHECK_EQ(next_pop, next_push);
}
