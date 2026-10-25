// Real-time contract: no heap allocation in any audio-thread path, nor in the 10 Hz scheduler
// hot path. Global operator new/delete are replaced in this test binary to count allocations.
#include "test_support/AcousticsTestUtils.hpp"
#include "acoustics/AcousticVoiceBank.hpp"
#include "acoustics/AuxReverbBus.hpp"
#include "acoustics/EnclosureDetector.hpp"
#include "acoustics/EnclosureMonitor.hpp"
#include "acoustics/FdnReverb.hpp"
#include "acoustics/OcclusionScheduler.hpp"
#include "acoustics/VoiceAcousticFilter.hpp"
#include "audio/AudioEngine.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <cstdlib>
#include <memory>
#include <new>
#include <span>
#include <vector>

namespace {
std::atomic<bool> g_counting{false};
std::atomic<size_t> g_allocations{0};

void* countedAlloc(std::size_t size) {
    if (g_counting.load(std::memory_order_relaxed)) {
        g_allocations.fetch_add(1, std::memory_order_relaxed);
    }
    void* p = std::malloc(size == 0 ? 1 : size);
    if (p == nullptr) {
        throw std::bad_alloc();
    }
    return p;
}

// RAII window: counts allocations made while alive.
class AllocationWindow {
public:
    AllocationWindow() noexcept {
        g_allocations.store(0);
        g_counting.store(true);
    }
    ~AllocationWindow() noexcept { g_counting.store(false); }
    AllocationWindow(const AllocationWindow&) = delete;
    AllocationWindow& operator=(const AllocationWindow&) = delete;
    [[nodiscard]] size_t count() const noexcept { return g_allocations.load(); }
};
} // namespace

void* operator new(std::size_t size) { return countedAlloc(size); }
void* operator new[](std::size_t size) { return countedAlloc(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

using namespace betacraft::acoustics;

TEST(RealtimeNoAlloc, CounterDetectsAllocations) {
    size_t n = 0;
    {
        const AllocationWindow w;
        auto p = std::make_unique<int>(3);
        n = w.count();
    }
    EXPECT_GE(n, 1u);
}

TEST(RealtimeNoAlloc, FdnProcessBlockDoesNotAllocate) {
    auto fdn = std::make_unique<FdnReverbProcessor>();
    std::vector<float> in(512, 0.25f);
    std::vector<float> l(512);
    std::vector<float> r(512);
    const AllocationWindow w;
    for (int b = 0; b < 200; ++b) {
        (void)fdn->processBlock(in, l, r);
        fdn->setParameters(0.9f, 0.4f);
    }
    fdn->reset();
    EXPECT_EQ(w.count(), 0u);
}

TEST(RealtimeNoAlloc, FullAudioBlockWith32VoicesDoesNotAllocate) {
    auto bus = std::make_unique<AuxReverbBus>();
    std::vector<VoiceAcousticFilter> voices(32);
    std::vector<float> voiceBuf(512);
    std::vector<float> out(1024);
    ReverbParams p;
    p.sendLevel = 0.4f;
    p.feedback = 0.9f;
    p.damping = 0.3f;
    const AllocationWindow w;
    for (int block = 0; block < 50; ++block) {
        bus->setReverbParams(p);
        const size_t n = bus->beginBlock(512);
        for (VoiceAcousticFilter& v : voices) {
            v.setOcclusionTarget({0.6f, 2000.0f});
            for (size_t i = 0; i < n; ++i) {
                const VoiceSample s = v.process(0.1f);
                out[2 * i] += s.dry;
                bus->accumulateSample(i, s.send);
            }
        }
        bus->accumulate(voiceBuf, 0.2f);
        bus->processAndMix(out, n);
    }
    EXPECT_EQ(w.count(), 0u);
}

TEST(RealtimeNoAlloc, SchedulerAndEnclosureProbesDoNotAllocate) {
    test::FakeVoxelGrid grid;
    grid.hollowBox({-6, -6, -6}, {6, 6, 6});
    auto sched = std::make_unique<OcclusionScheduler>();
    for (uint32_t h = 0; h < OcclusionScheduler::CAPACITY; ++h) {
        ASSERT_TRUE(sched->addSource(h, {static_cast<float>(h % 7), 1.5f, 2.5f}));
    }
    const EnclosureDetector det;
    EnclosureMonitor monitor;
    (void)EnclosureDetector::probeDirections(); // Static table initialised outside the window
    const AllocationWindow w;
    for (int f = 0; f < 30; ++f) {
        (void)sched->update(0.1 * f, {0.5f, 0.5f, 0.5f}, grid);
        (void)sched->interpolatedTarget(3u, 0.1 * f + 0.05);
        (void)det.analyze({0.5f, 0.5f, 0.5f}, grid);
        (void)monitor.update(0.1 * f, {0.5f + 0.2f * static_cast<float>(f), 0.5f, 0.5f}, grid);
        (void)sched->setSourcePosition(3u, {1.0f, 2.0f, 3.0f});
    }
    EXPECT_EQ(w.count(), 0u);
}

TEST(RealtimeNoAlloc, VoiceBankDrainAndMixDoNotAllocate) {
    auto bank = std::make_unique<AcousticVoiceBank>();
    std::vector<float> out(1024);
    ReverbParams env;
    env.sendLevel = 0.4f;
    env.feedback = 0.9f;
    env.damping = 0.3f;
    const AllocationWindow w;
    for (int block = 0; block < 40; ++block) {
        // Producer side (normally another thread) and consumer side in one loop.
        (void)bank->post(AcousticCommand::environment(env));
        const uint32_t generation = static_cast<uint32_t>(block / 10) * 64u; // Sounds restart every 10 blocks
        for (uint32_t v = 0; v < AcousticVoiceBank::MAX_VOICES; ++v) {
            if (block % 10 == 0) {
                (void)bank->startVoice(v, generation + v + 1u, {0.7f, 3000.0f}, 0.2f);
            }
            (void)bank->post(AcousticCommand::occlusion(generation + v + 1u, {0.5f, 1500.0f}));
        }
        (void)bank->post(AcousticCommand::occlusion(9999u, {0.5f, 1500.0f})); // Stale
        (void)bank->drainCommands();
        const size_t n = bank->beginBlock(512);
        for (uint32_t v = 0; v < AcousticVoiceBank::MAX_VOICES; ++v) {
            for (size_t i = 0; i < n; ++i) {
                out[2 * i] += bank->processVoiceSample(v, i, 0.1f);
            }
        }
        bank->endBlock(out, n);
    }
    EXPECT_EQ(w.count(), 0u);
    EXPECT_EQ(bank->staleCommands(), 40u); // Only the deliberate stale post per block
}

TEST(RealtimeNoAlloc, LongCallbacksVoiceStealingAndStaleCommandsDoNotAllocate) {
    auto bank = std::make_unique<AcousticVoiceBank>();
    std::vector<float> out(2 * 1500);
    VoiceHandleAllocator handles;
    const AllocationWindow w;
    for (int cb = 0; cb < 30; ++cb) {
        const uint32_t h = handles.next();
        (void)bank->startVoice(static_cast<size_t>(cb) % AcousticVoiceBank::MAX_VOICES, h, {0.5f, 2000.0f});
        (void)bank->post(AcousticCommand::occlusion(h, {0.4f, 1000.0f}));
        (void)bank->post(AcousticCommand::occlusion(h - 1u, {0.4f, 1000.0f})); // Possibly stolen / stale
        bank->stopVoice(static_cast<size_t>(cb + 7) % AcousticVoiceBank::MAX_VOICES);
        (void)bank->drainCommands();
        bank->processBlock(out, 1500, [&](std::span<float> sub, size_t, size_t frames) {
            for (size_t v = 0; v < AcousticVoiceBank::MAX_VOICES; ++v) {
                for (size_t i = 0; i < frames; ++i) {
                    sub[2 * i] += bank->processVoiceSample(v, i, 0.1f);
                }
            }
        });
    }
    EXPECT_EQ(w.count(), 0u);
    EXPECT_EQ(bank->droppedSendFrames(), 0u);
}

// The integrated callback: AudioEngine's mixing path draining play / stop / acoustic commands and
// routing voices through the bank, with callbacks shorter and longer than the aux bus block.
TEST(RealtimeNoAlloc, AudioEngineMixingPathDoesNotAllocate) {
    betacraft::audio::AudioEngine engine;
    std::vector<float> out(2 * 1500);
    const ReverbParams cave{1.2f, 0.0f, 0.25f, 3.0f};
    const AllocationWindow w;
    for (uint32_t cb = 0; cb < 40; ++cb) {
        const auto voice = engine.playSound(betacraft::audio::SoundEffect::BreakStone, 0.4f, 1.0f,
                                            betacraft::audio::VoiceAcoustics{0.5f, 3000.0f, 0.15f});
        (void)engine.playSound(betacraft::audio::SoundEffect::UiClick, 0.3f, 1.0f); // Dry voice
        (void)engine.postAcoustic(AcousticCommand::occlusion(voice.handle, {0.3f, 900.0f}));
        (void)engine.postAcoustic(AcousticCommand::environment(cave, cb == 0));
        if (cb % 13 == 12) {
            engine.stopAll();
        }
        ASSERT_TRUE(engine.renderFrames(out.data(), cb % 2 == 0 ? 256u : 1500u));
    }
    EXPECT_EQ(w.count(), 0u);
    EXPECT_EQ(engine.acousticBank().droppedSendFrames(), 0u);
}
