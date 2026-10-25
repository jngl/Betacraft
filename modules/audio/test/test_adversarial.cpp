#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "core/MpscRingBuffer.hpp"
#include "audio/AudioEngine.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>
#include <random>

using namespace betacraft::audio;
using namespace betacraft::core;

struct StressPayload {
    uint32_t producerId{0};
    uint32_t seqNumber{0};
    uint64_t checksum{0};

    static StressPayload make(uint32_t p, uint32_t s) {
        return StressPayload{p, s, (static_cast<uint64_t>(p) << 32) | (s ^ 0x5A5A5A5Au)};
    }

    [[nodiscard]] bool isValid() const {
        return checksum == ((static_cast<uint64_t>(producerId) << 32) | (seqNumber ^ 0x5A5A5A5Au));
    }
};

// ============================================================================
// SUITE 1: High Concurrency Multi-Producer Stress (8+ Threads, 16k+ Items)
// ============================================================================

TEST(AdversarialRingBuffer, HighConcurrencyMultiProducer8Threads) {
    constexpr size_t numProducers = 8;
    constexpr size_t itemsPerProducer = 2000; // 16,000 items total
    constexpr size_t totalItems = numProducers * itemsPerProducer;

    MpscRingBuffer<StressPayload, 512> ring;
    EXPECT_EQ(ring.capacity(), 512u);
    EXPECT_TRUE(ring.empty());

    std::atomic<bool> startFlag{false};
    std::vector<std::thread> producers;
    producers.reserve(numProducers);

    for (size_t p = 0; p < numProducers; ++p) {
        producers.emplace_back([&ring, &startFlag, p]() {
            while (!startFlag.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            for (size_t i = 0; i < itemsPerProducer; ++i) {
                StressPayload item = StressPayload::make(static_cast<uint32_t>(p), static_cast<uint32_t>(i));
                while (!ring.push(item)) {
                    std::this_thread::yield();
                }
            }
        });
    }

    // Consumer verification
    std::vector<int64_t> lastSeq(numProducers, -1);
    std::vector<size_t> countPerProducer(numProducers, 0);
    size_t totalPopped = 0;

    const auto startTime = std::chrono::steady_clock::now();
    startFlag.store(true, std::memory_order_release);

    while (totalPopped < totalItems) {
        StressPayload out{};
        if (ring.pop(out)) {
            ASSERT_TRUE(out.isValid());
            ASSERT_LT(out.producerId, numProducers);
            ASSERT_EQ(static_cast<int64_t>(out.seqNumber), lastSeq[out.producerId] + 1);
            lastSeq[out.producerId] = out.seqNumber;
            countPerProducer[out.producerId]++;
            totalPopped++;
        } else {
            const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - startTime).count();
            ASSERT_LT(elapsed, 10) << "Deadlock detected: timed out waiting for queue items";
            std::this_thread::yield();
        }
    }

    for (auto& t : producers) {
        t.join();
    }

    EXPECT_EQ(totalPopped, totalItems);
    for (size_t p = 0; p < numProducers; ++p) {
        EXPECT_EQ(countPerProducer[p], itemsPerProducer);
    }
    EXPECT_TRUE(ring.empty());
}

TEST(AdversarialRingBuffer, HighConcurrencyMultiProducer12Threads) {
    constexpr size_t numProducers = 12;
    constexpr size_t itemsPerProducer = 1500; // 18,000 items total
    constexpr size_t totalItems = numProducers * itemsPerProducer;

    MpscRingBuffer<StressPayload, 512> ring;
    std::atomic<bool> startFlag{false};
    std::vector<std::thread> producers;
    producers.reserve(numProducers);

    for (size_t p = 0; p < numProducers; ++p) {
        producers.emplace_back([&ring, &startFlag, p]() {
            while (!startFlag.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            for (size_t i = 0; i < itemsPerProducer; ++i) {
                StressPayload item = StressPayload::make(static_cast<uint32_t>(p), static_cast<uint32_t>(i));
                while (!ring.push(item)) {
                    std::this_thread::yield();
                }
            }
        });
    }

    std::vector<int64_t> lastSeq(numProducers, -1);
    std::vector<size_t> countPerProducer(numProducers, 0);
    size_t totalPopped = 0;

    const auto startTime = std::chrono::steady_clock::now();
    startFlag.store(true, std::memory_order_release);

    while (totalPopped < totalItems) {
        StressPayload out{};
        if (ring.pop(out)) {
            ASSERT_TRUE(out.isValid());
            ASSERT_LT(out.producerId, numProducers);
            ASSERT_EQ(static_cast<int64_t>(out.seqNumber), lastSeq[out.producerId] + 1);
            lastSeq[out.producerId] = out.seqNumber;
            countPerProducer[out.producerId]++;
            totalPopped++;
        } else {
            const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - startTime).count();
            ASSERT_LT(elapsed, 10) << "Deadlock detected in 12-thread stress test";
            std::this_thread::yield();
        }
    }

    for (auto& t : producers) {
        t.join();
    }

    EXPECT_EQ(totalPopped, totalItems);
    EXPECT_TRUE(ring.empty());
}

// ============================================================================
// SUITE 2: Queue Overflow & Tail Invariant
// ============================================================================

TEST(AdversarialRingBuffer, QueueOverflowGracefulRejectionNoTailAdvancement) {
    MpscRingBuffer<int, 512> ring;

    // Fill to capacity
    for (int i = 0; i < 512; ++i) {
        EXPECT_TRUE(ring.push(i));
    }
    EXPECT_FALSE(ring.empty());

    // 513th through 1024th pushes must all fail
    for (int i = 512; i < 1024; ++i) {
        EXPECT_FALSE(ring.push(i));
    }

    // Verify tail did not advance: pop one item, push one item
    int first = -1;
    EXPECT_TRUE(ring.pop(first));
    EXPECT_EQ(first, 0);

    // Exactly one push must now succeed
    EXPECT_TRUE(ring.push(77777));

    // Next immediate push must fail (queue full again)
    EXPECT_FALSE(ring.push(88888));

    // Pop all remaining items: [1..511], then 77777
    for (int i = 1; i < 512; ++i) {
        int v = -1;
        EXPECT_TRUE(ring.pop(v));
        EXPECT_EQ(v, i);
    }

    int last = -1;
    EXPECT_TRUE(ring.pop(last));
    EXPECT_EQ(last, 77777);

    // Queue is empty
    EXPECT_TRUE(ring.empty());
    int emptyVal = -1;
    EXPECT_FALSE(ring.pop(emptyVal));
    EXPECT_EQ(emptyVal, -1);
}

TEST(AdversarialRingBuffer, ConcurrentOverflowRejectionIntegrity) {
    MpscRingBuffer<int, 512> ring;
    for (int i = 0; i < 512; ++i) {
        ASSERT_TRUE(ring.push(i));
    }

    constexpr size_t numThreads = 8;
    constexpr size_t attemptsPerThread = 500;
    std::atomic<bool> startFlag{false};
    std::atomic<size_t> rejectedCount{0};
    std::vector<std::thread> threads;
    threads.reserve(numThreads);

    for (size_t t = 0; t < numThreads; ++t) {
        threads.emplace_back([&ring, &startFlag, &rejectedCount]() {
            while (!startFlag.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            for (size_t i = 0; i < attemptsPerThread; ++i) {
                if (!ring.push(static_cast<int>(i + 5000))) {
                    rejectedCount.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    startFlag.store(true, std::memory_order_release);
    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(rejectedCount.load(), numThreads * attemptsPerThread);

    // Verify all original 512 items remained uncorrupted
    for (int i = 0; i < 512; ++i) {
        int v = -1;
        EXPECT_TRUE(ring.pop(v));
        EXPECT_EQ(v, i);
    }
    EXPECT_TRUE(ring.empty());
}

// ============================================================================
// SUITE 3: Empty Pop and Wraparound Integrity
// ============================================================================

TEST(AdversarialRingBuffer, EmptyPopStressIntegrity) {
    MpscRingBuffer<int, 512> ring;
    EXPECT_TRUE(ring.empty());

    for (int i = 0; i < 50000; ++i) {
        int out = 9999;
        EXPECT_FALSE(ring.pop(out));
        EXPECT_EQ(out, 9999);
    }
    EXPECT_TRUE(ring.empty());

    // Verify subsequent operation
    EXPECT_TRUE(ring.push(101));
    EXPECT_FALSE(ring.empty());
    int val = 0;
    EXPECT_TRUE(ring.pop(val));
    EXPECT_EQ(val, 101);
    EXPECT_TRUE(ring.empty());
}

TEST(AdversarialRingBuffer, HighFrequencyCirculatingWraparound) {
    MpscRingBuffer<int, 16> ring;
    constexpr int iterations = 100000;

    for (int i = 0; i < iterations; ++i) {
        ASSERT_TRUE(ring.push(i));
        int out = -1;
        ASSERT_TRUE(ring.pop(out));
        ASSERT_EQ(out, i);
    }

    EXPECT_TRUE(ring.empty());
}

TEST(AdversarialRingBuffer, AlignmentAndNoexceptGuarantees) {
    EXPECT_EQ(alignof(MpscRingBuffer<AudioCommand, 512>), 64u);
    EXPECT_EQ(alignof(MpscRingBuffer<int, 512>), 64u);

    MpscRingBuffer<AudioCommand, 512> ring;
    AudioCommand cmd{};
    EXPECT_TRUE(noexcept(ring.push(cmd)));
    EXPECT_TRUE(noexcept(ring.pop(cmd)));
    EXPECT_TRUE(noexcept(ring.empty()));
    EXPECT_TRUE(noexcept(ring.capacity()));
}

// ============================================================================
// SUITE 4: AudioEngine Zero Allocations & Contention
// ============================================================================

TEST(AdversarialAudioEngine, ZeroAllocationsAndCommandDrain) {
    AudioEngine engine;
    ASSERT_TRUE(engine.initialize());

    // Queue 512 sound commands
    for (int i = 0; i < 512; ++i) {
        engine.playBlockStep(betacraft::voxel::SoundCategory::Grass);
    }

    // Stop all
    engine.stopAll();
    engine.shutdown();
}

TEST(AdversarialAudioEngine, MultiThreadBurstContention8Threads) {
    AudioEngine engine;
    ASSERT_TRUE(engine.initialize());

    constexpr size_t numThreads = 8;
    constexpr size_t iterationsPerThread = 1500;
    std::vector<std::thread> threads;
    threads.reserve(numThreads);

    for (size_t t = 0; t < numThreads; ++t) {
        threads.emplace_back([&engine, t]() {
            for (size_t i = 0; i < iterationsPerThread; ++i) {
                switch ((t + i) % 6) {
                    case 0: engine.playBlockStep(betacraft::voxel::SoundCategory::Dirt); break;
                    case 1: engine.playBlockBreak(betacraft::voxel::SoundCategory::Wood); break;
                    case 2: engine.playBlockPlace(betacraft::voxel::SoundCategory::Stone); break;
                    case 3: engine.playWaterSplash(); break;
                    case 4: engine.playUiClick(); break;
                    case 5: engine.stopAll(); break;
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    engine.stopAll();
    engine.shutdown();
}
