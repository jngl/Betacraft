#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "audio/MpscRingBuffer.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

using namespace betacraft::audio;

struct DummyAudioCmd {
    uint32_t soundId{0};
    float volume{1.0f};
    float pitch{1.0f};
    int producerId{0};
};

TEST(MpscRingBufferTest, PowerOfTwoCapacityAndTraits) {
    MpscRingBuffer<DummyAudioCmd, 512> ring;
    EXPECT_EQ(ring.capacity(), 512u);
    EXPECT_TRUE(ring.empty());

    // Verify 64-byte alignment to eliminate false sharing
    EXPECT_EQ(alignof(MpscRingBuffer<DummyAudioCmd, 512>), 64u);

    // Verify noexcept operations for real-time safety
    EXPECT_TRUE(noexcept(ring.push(DummyAudioCmd{})));
    DummyAudioCmd out{};
    EXPECT_TRUE(noexcept(ring.pop(out)));
}

TEST(MpscRingBufferTest, SingleProducerSingleConsumerFIFO) {
    MpscRingBuffer<int, 512> ring;
    EXPECT_TRUE(ring.empty());

    // Push up to full capacity
    for (int i = 0; i < 512; ++i) {
        EXPECT_TRUE(ring.push(i));
    }
    EXPECT_FALSE(ring.empty());

    // 513th push must return false (bounded queue full)
    EXPECT_FALSE(ring.push(9999));

    // Pop all items in FIFO order
    for (int i = 0; i < 512; ++i) {
        int val = -1;
        EXPECT_TRUE(ring.pop(val));
        EXPECT_EQ(val, i);
    }

    EXPECT_TRUE(ring.empty());

    // Pop on empty buffer must return false
    int out = -1;
    EXPECT_FALSE(ring.pop(out));
    EXPECT_EQ(out, -1);
}

TEST(MpscRingBufferTest, WraparoundIntegrity) {
    MpscRingBuffer<int, 32> ring;
    constexpr int iterations = 10000;

    for (int i = 0; i < iterations; ++i) {
        EXPECT_TRUE(ring.push(i));
        int val = -1;
        EXPECT_TRUE(ring.pop(val));
        EXPECT_EQ(val, i);
    }

    EXPECT_TRUE(ring.empty());
}

TEST(MpscRingBufferTest, MultiProducerContentionStress) {
    MpscRingBuffer<DummyAudioCmd, 512> ring;
    constexpr int numProducers = 4;
    constexpr int itemsPerProducer = 2000;
    constexpr int totalItems = numProducers * itemsPerProducer;

    std::atomic<bool> start{false};
    std::vector<std::thread> producers;
    producers.reserve(numProducers);

    for (int p = 0; p < numProducers; ++p) {
        producers.emplace_back([&ring, &start, p]() {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            for (int i = 0; i < itemsPerProducer; ++i) {
                DummyAudioCmd cmd{static_cast<uint32_t>(i), 1.0f, 1.0f, p};
                while (!ring.push(cmd)) {
                    std::this_thread::yield();
                }
            }
        });
    }

    // Consumer thread
    std::vector<int> lastSeen(numProducers, -1);
    std::vector<int> countPerProducer(numProducers, 0);
    int totalPopped = 0;

    start.store(true, std::memory_order_release);

    while (totalPopped < totalItems) {
        DummyAudioCmd cmd{};
        if (ring.pop(cmd)) {
            EXPECT_EQ(cmd.soundId, static_cast<uint32_t>(lastSeen[cmd.producerId] + 1));
            lastSeen[cmd.producerId] = static_cast<int>(cmd.soundId);
            countPerProducer[cmd.producerId]++;
            totalPopped++;
        } else {
            std::this_thread::yield();
        }
    }

    for (auto& t : producers) {
        t.join();
    }

    EXPECT_EQ(totalPopped, totalItems);
    for (int p = 0; p < numProducers; ++p) {
        EXPECT_EQ(countPerProducer[p], itemsPerProducer);
    }
}
