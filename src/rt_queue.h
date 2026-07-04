#pragma once
#ifndef GROOVYDAISY_RT_QUEUE_H
#define GROOVYDAISY_RT_QUEUE_H

#include <stdint.h>
#include <stddef.h>
#include <atomic>

/**
 * Single-producer / single-consumer lock-free ring buffer.
 *
 * The only sanctioned channel between the audio callback and the main
 * loop. Push never blocks: on a full ring the item is dropped and the
 * drop counter increments — callers surface drops via diagnostics, never
 * silently. On the single-core STM32H7 (and on the host test build),
 * acquire/release atomics are sufficient: they order memory against
 * IRQ preemption and compiler reordering.
 */
template <typename T, size_t N>
class SpscRing
{
    static_assert((N & (N - 1)) == 0, "N must be a power of two");

  public:
    /** Producer side. Returns false (and counts a drop) when full. */
    bool Push(const T& item)
    {
        uint32_t head = head_.load(std::memory_order_relaxed);
        uint32_t tail = tail_.load(std::memory_order_acquire);
        if(head - tail >= N)
        {
            dropped_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        buf_[head & (N - 1)] = item;
        head_.store(head + 1, std::memory_order_release);
        return true;
    }

    /** Consumer side. Returns false when empty. */
    bool Pop(T& out)
    {
        uint32_t tail = tail_.load(std::memory_order_relaxed);
        uint32_t head = head_.load(std::memory_order_acquire);
        if(tail == head)
        {
            return false;
        }
        out = buf_[tail & (N - 1)];
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }

    uint32_t Size() const
    {
        return head_.load(std::memory_order_acquire)
               - tail_.load(std::memory_order_acquire);
    }

    bool Empty() const { return Size() == 0; }

    /** Total items dropped because the ring was full. Never resets. */
    uint32_t Dropped() const
    {
        return dropped_.load(std::memory_order_relaxed);
    }

  private:
    T buf_[N];
    std::atomic<uint32_t> head_{0};
    std::atomic<uint32_t> tail_{0};
    std::atomic<uint32_t> dropped_{0};
};

#endif // GROOVYDAISY_RT_QUEUE_H
