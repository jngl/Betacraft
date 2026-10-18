#include "streaming/PingPongStaging.hpp"

#include "StreamingTestHelpers.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <latch>
#include <memory>
#include <thread>
#include <vector>

using betacraft::streaming::PingPongStaging;

TEST(PingPongStaging, EmptyStagingYieldsNothing) {
    PingPongStaging<int> s;
    int out = -1;
    EXPECT_FALSE(s.tryPop(out));
    EXPECT_EQ(out, -1);
    EXPECT_EQ(s.peek(), nullptr);
    EXPECT_EQ(s.frontRemaining(), 0u);
    EXPECT_EQ(s.backSize(), 0u);
    EXPECT_EQ(s.swapCount(), 0u);
}

TEST(PingPongStaging, PreservesPublicationOrder) {
    PingPongStaging<int> s;
    for (int i = 0; i < 5; ++i) s.publish(i);
    EXPECT_EQ(s.backSize(), 5u);
    for (int i = 0; i < 5; ++i) {
        int out = -1;
        ASSERT_TRUE(s.tryPop(out));
        EXPECT_EQ(out, i);
    }
    int out = -1;
    EXPECT_FALSE(s.tryPop(out));
}

TEST(PingPongStaging, SwapHappensOnlyWhenFrontIsExhausted) {
    PingPongStaging<int> s;
    s.publish(1);
    s.publish(2);
    ASSERT_NE(s.peek(), nullptr); // triggers the swap
    EXPECT_EQ(s.swapCount(), 1u);
    EXPECT_EQ(s.frontRemaining(), 2u);
    EXPECT_EQ(s.backSize(), 0u);

    s.publish(3); // lands in the back buffer, front untouched
    EXPECT_EQ(s.backSize(), 1u);
    EXPECT_EQ(s.frontRemaining(), 2u);

    int out = 0;
    ASSERT_TRUE(s.tryPop(out));
    EXPECT_EQ(out, 1);
    ASSERT_TRUE(s.tryPop(out));
    EXPECT_EQ(out, 2);
    EXPECT_EQ(s.swapCount(), 1u);
    ASSERT_TRUE(s.tryPop(out)); // second swap
    EXPECT_EQ(out, 3);
    EXPECT_EQ(s.swapCount(), 2u);
}

TEST(PingPongStaging, PeekDoesNotConsume) {
    PingPongStaging<int> s;
    s.publish(7);
    ASSERT_NE(s.peek(), nullptr);
    EXPECT_EQ(*s.peek(), 7);
    EXPECT_EQ(*s.peek(), 7);
    s.pop();
    EXPECT_EQ(s.peek(), nullptr);
}

TEST(PingPongStaging, SupportsMoveOnlyPayloads) {
    PingPongStaging<std::unique_ptr<int>> s;
    s.publish(std::make_unique<int>(42));
    std::unique_ptr<int> out;
    ASSERT_TRUE(s.tryPop(out));
    ASSERT_NE(out, nullptr);
    EXPECT_EQ(*out, 42);
}

TEST(PingPongStaging, ClearDropsBothBuffers) {
    PingPongStaging<int> s;
    s.publish(1);
    s.publish(2);
    (void)s.peek();
    s.publish(3);
    s.clear();
    EXPECT_EQ(s.frontRemaining(), 0u);
    EXPECT_EQ(s.backSize(), 0u);
    int out = 0;
    EXPECT_FALSE(s.tryPop(out));
}

TEST(PingPongStaging, BatchPublishAppendsInOrder) {
    PingPongStaging<int> s;
    std::vector<int> batch{4, 5, 6};
    s.publishBatch(std::move(batch));
    std::vector<int> got;
    int out = 0;
    while (s.tryPop(out)) got.push_back(out);
    EXPECT_EQ(got, (std::vector<int>{4, 5, 6}));
}

// Producers publish concurrently with a live consumer; every item must arrive exactly
// once and per-producer FIFO order must hold. Run under TSAN to prove race freedom.
TEST(PingPongStaging, ConcurrentProducersSingleConsumerExactlyOnceInOrder) {
    constexpr int kProducers = 4;
    constexpr int kPerProducer = 5000;
    PingPongStaging<std::pair<int, int>> s;
    std::latch start(kProducers + 1);
    std::vector<int> lastSeen(kProducers, -1);
    int received = 0;
    bool orderOk = true;

    {
        std::vector<std::jthread> producers;
        for (int p = 0; p < kProducers; ++p) {
            producers.emplace_back([&, p] {
                start.arrive_and_wait();
                for (int i = 0; i < kPerProducer; ++i) s.publish({p, i});
            });
        }
        start.arrive_and_wait();
        const auto deadline = std::chrono::steady_clock::now() + betacraft::streaming::test::kGenerousTimeout;
        std::pair<int, int> item;
        while (received < kProducers * kPerProducer && std::chrono::steady_clock::now() < deadline) {
            if (s.tryPop(item)) {
                if (item.second != lastSeen[static_cast<size_t>(item.first)] + 1) orderOk = false;
                lastSeen[static_cast<size_t>(item.first)] = item.second;
                ++received;
            } else {
                std::this_thread::yield();
            }
        }
    }
    EXPECT_EQ(received, kProducers * kPerProducer);
    EXPECT_TRUE(orderOk);
    for (int p = 0; p < kProducers; ++p) EXPECT_EQ(lastSeen[static_cast<size_t>(p)], kPerProducer - 1);
}
