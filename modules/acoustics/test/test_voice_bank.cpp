// Audio-thread side: MPSC acoustic commands applied to 32 voice filters + the shared aux bus.
#include "acoustics/AcousticVoiceBank.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <memory>
#include <thread>
#include <type_traits>
#include <vector>

namespace {

using namespace betacraft::acoustics;

constexpr size_t N = AuxReverbBus::MAX_BLOCK_FRAMES;

std::unique_ptr<AcousticVoiceBank> makeBank() { return std::make_unique<AcousticVoiceBank>(); }

ReverbParams cave() {
    ReverbParams p;
    p.sendLevel = 0.4f;
    p.feedback = 0.9f;
    p.damping = 0.3f;
    p.decaySeconds = 0.0f; // Explicit-feedback mode: the bus uses 0.9 as given
    return p;
}

} // namespace

// --- AcousticCommand payload ------------------------------------------------------------------------

TEST(AcousticCommand, IsSmallAndTriviallyCopyableForTheMpscQueue) {
    EXPECT_TRUE(std::is_trivially_copyable_v<AcousticCommand>);
    EXPECT_LE(sizeof(AcousticCommand), 32u);
}

TEST(AcousticCommand, FactoriesFillTheRightFields) {
    const AcousticCommand o = AcousticCommand::occlusion(5u, {0.5f, 1234.0f});
    EXPECT_EQ(o.type, AcousticCommandType::VoiceOcclusion);
    EXPECT_EQ(o.voiceHandle, 5u);
    EXPECT_FLOAT_EQ(o.gain, 0.5f);
    EXPECT_FLOAT_EQ(o.cutoffHz, 1234.0f);

    const AcousticCommand r = AcousticCommand::reverbSend(2u, 0.6f);
    EXPECT_EQ(r.type, AcousticCommandType::VoiceReverbSend);
    EXPECT_EQ(r.voiceHandle, 2u);
    EXPECT_FLOAT_EQ(r.send, 0.6f);

    const AcousticCommand e = AcousticCommand::environment(cave(), true);
    EXPECT_EQ(e.type, AcousticCommandType::Environment);
    EXPECT_FLOAT_EQ(e.send, 0.4f);
    EXPECT_FLOAT_EQ(e.feedback, 0.9f);
    EXPECT_FLOAT_EQ(e.damping, 0.3f);
    EXPECT_TRUE(e.snap);
}

TEST(AcousticCommand, FromSchedulerUpdateCarriesTheProducerHandle) {
    const OcclusionUpdate u{12u, 0.7f, 5000.0f, 2u, false};
    const AcousticCommand c = AcousticCommand::fromUpdate(u);
    EXPECT_EQ(c.type, AcousticCommandType::VoiceOcclusion);
    EXPECT_EQ(c.voiceHandle, 12u);
    EXPECT_FLOAT_EQ(c.gain, 0.7f);
    EXPECT_FLOAT_EQ(c.cutoffHz, 5000.0f);
}

// --- AcousticVoiceBank: command application ---------------------------------------------------------

TEST(AcousticVoiceBank, HasOneFilterPerMixerVoiceAndOneSharedBus) {
    const auto bank = makeBank();
    EXPECT_EQ(AcousticVoiceBank::MAX_VOICES, 32u);
    EXPECT_EQ(bank->bus().reverb().memoryBytes(), FdnReverbProcessor::TOTAL_DELAY_SAMPLES * sizeof(float));
}

TEST(AcousticVoiceBank, OcclusionCommandRampsOnlyTheAddressedVoice) {
    auto bank = makeBank();
    ASSERT_TRUE(bank->startVoice(3u, 103u, {1.0f, 20000.0f}));
    ASSERT_TRUE(bank->startVoice(4u, 104u, {1.0f, 20000.0f}));
    ASSERT_TRUE(bank->post(AcousticCommand::occlusion(103u, {0.5f, 1000.0f})));
    EXPECT_EQ(bank->drainCommands(), 1u);
    // Smoothed, not snapped: still at unity right after the command lands...
    EXPECT_FLOAT_EQ(bank->voice(3u).currentGain(), 1.0f);
    for (int i = 0; i < 44100; ++i) {
        (void)bank->processVoiceSample(3u, 0, 0.0f);
    }
    // ... and converged after a while. Other voices are untouched.
    EXPECT_NEAR(bank->voice(3u).currentGain(), 0.5f, 1e-4f);
    EXPECT_NEAR(bank->voice(3u).currentCoefficient(), onePoleCoefficient(1000.0f, DEFAULT_SAMPLE_RATE), 1e-4f);
    EXPECT_FLOAT_EQ(bank->voice(4u).currentGain(), 1.0f);
    EXPECT_FLOAT_EQ(bank->voice(4u).currentCoefficient(), onePoleCoefficient(20000.0f, DEFAULT_SAMPLE_RATE));
}

TEST(AcousticVoiceBank, EnvironmentCommandDrivesTheSharedBus) {
    auto bank = makeBank();
    ASSERT_TRUE(bank->post(AcousticCommand::environment(cave(), true)));
    EXPECT_EQ(bank->drainCommands(), 1u);
    EXPECT_FLOAT_EQ(bank->bus().reverb().feedbackGain(), 0.9f);
    EXPECT_FLOAT_EQ(bank->bus().reverb().damping(), 0.3f);
}

TEST(AcousticVoiceBank, InvalidVoiceCommandsAreRejectedAndCounted) {
    auto bank = makeBank();
    ASSERT_TRUE(bank->startVoice(0u, 1u, {1.0f, 20000.0f}));
    ASSERT_TRUE(bank->post(AcousticCommand::occlusion(32u, {0.1f, 400.0f})));                   // Unbound
    ASSERT_TRUE(bank->post(AcousticCommand::reverbSend(1000u, 0.2f)));                          // Unbound
    ASSERT_TRUE(bank->post(AcousticCommand::occlusion(INVALID_VOICE_HANDLE, {0.1f, 400.0f}))); // Invalid
    ASSERT_TRUE(bank->post(AcousticCommand::occlusion(1u, {0.5f, 400.0f})));
    EXPECT_EQ(bank->drainCommands(), 1u);
    EXPECT_EQ(bank->rejectedCommands(), 1u);
    EXPECT_EQ(bank->staleCommands(), 2u);
}

TEST(AcousticVoiceBank, UnknownCommandTypeIsRejected) {
    auto bank = makeBank();
    AcousticCommand bogus = AcousticCommand::occlusion(0u, {0.5f, 400.0f});
    bogus.type = static_cast<AcousticCommandType>(200);
    EXPECT_FALSE(bank->apply(bogus));
    EXPECT_EQ(bank->rejectedCommands(), 1u);
}

TEST(AcousticVoiceBank, CommandsApplyInFifoOrderSoTheLatestWins) {
    auto bank = makeBank();
    ASSERT_TRUE(bank->startVoice(2u, 20u, {1.0f, 20000.0f}));
    ASSERT_TRUE(bank->post(AcousticCommand::occlusion(20u, {0.9f, 20000.0f})));
    ASSERT_TRUE(bank->post(AcousticCommand::occlusion(20u, {0.3f, 20000.0f})));
    EXPECT_EQ(bank->drainCommands(), 2u);
    for (int i = 0; i < 44100; ++i) {
        (void)bank->processVoiceSample(2u, 0, 0.0f);
    }
    EXPECT_NEAR(bank->voice(2u).currentGain(), 0.3f, 1e-4f);
}

TEST(AcousticVoiceBank, DrainHonoursItsBudgetAndKeepsTheRest) {
    auto bank = makeBank();
    for (uint32_t i = 0; i < 10u; ++i) {
        ASSERT_TRUE(bank->startVoice(i, i + 1u, {1.0f, 20000.0f}));
        ASSERT_TRUE(bank->post(AcousticCommand::occlusion(i + 1u, {0.5f, 2000.0f})));
    }
    EXPECT_EQ(bank->drainCommands(4u), 4u);
    EXPECT_EQ(bank->drainCommands(), 6u);
    EXPECT_EQ(bank->drainCommands(), 0u);
}

TEST(AcousticVoiceBank, FullQueueRejectsPostsWithoutBlocking) {
    auto bank = makeBank();
    ASSERT_TRUE(bank->startVoice(0u, 1u, {1.0f, 20000.0f}));
    size_t accepted = 0;
    for (size_t i = 0; i < AcousticVoiceBank::QUEUE_CAPACITY + 10u; ++i) {
        accepted += bank->post(AcousticCommand::occlusion(1u, {0.5f, 2000.0f})) ? 1u : 0u;
    }
    EXPECT_EQ(accepted, AcousticVoiceBank::QUEUE_CAPACITY);
    EXPECT_EQ(bank->drainCommands(), AcousticVoiceBank::QUEUE_CAPACITY);
    EXPECT_TRUE(bank->post(AcousticCommand::occlusion(1u, {0.5f, 2000.0f})));
}

TEST(AcousticVoiceBank, ConcurrentProducersLoseNoAcceptedCommand) {
    auto bank = makeBank();
    for (uint32_t v = 0; v < AcousticVoiceBank::MAX_VOICES; ++v) {
        ASSERT_TRUE(bank->startVoice(v, v + 1u, {1.0f, 20000.0f}));
    }
    constexpr int PRODUCERS = 4;
    constexpr int PER_PRODUCER = 2000;
    std::atomic<size_t> accepted{0};
    std::atomic<bool> done{false};
    size_t drained = 0;
    std::thread consumer([&] {
        while (!done.load(std::memory_order_acquire)) {
            drained += bank->drainCommands();
        }
        drained += bank->drainCommands();
    });
    std::vector<std::thread> producers;
    for (int p = 0; p < PRODUCERS; ++p) {
        producers.emplace_back([&, p] {
            for (int i = 0; i < PER_PRODUCER; ++i) {
                const auto handle = static_cast<uint32_t>((p * PER_PRODUCER + i) % 32) + 1u;
                if (bank->post(AcousticCommand::occlusion(handle, {0.5f, 3000.0f}))) {
                    accepted.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }
    for (std::thread& t : producers) {
        t.join();
    }
    done.store(true, std::memory_order_release);
    consumer.join();
    EXPECT_EQ(drained, accepted.load());
    EXPECT_GT(accepted.load(), 0u);
    EXPECT_EQ(bank->rejectedCommands(), 0u);
    EXPECT_EQ(bank->staleCommands(), 0u);
}

// --- AcousticVoiceBank: per-block mixing ------------------------------------------------------------

TEST(AcousticVoiceBank, OutOfRangeVoicePassesSampleThroughUnprocessed) {
    auto bank = makeBank();
    (void)bank->beginBlock(N);
    EXPECT_FLOAT_EQ(bank->processVoiceSample(99u, 0, 0.75f), 0.75f);
}

TEST(AcousticVoiceBank, VoicesFeedTheSharedReverbThroughTheirSends) {
    auto bank = makeBank();
    ASSERT_TRUE(bank->post(AcousticCommand::environment(cave(), true)));
    (void)bank->drainCommands();
    ASSERT_TRUE(bank->startVoice(0u, 1u, {1.0f, 20000.0f}, 1.0f));
    ASSERT_TRUE(bank->startVoice(1u, 2u, {1.0f, 20000.0f}, 0.0f));

    std::vector<float> out(2 * N);
    double wetEnergy = 0.0;
    for (int b = 0; b < 8; ++b) {
        std::fill(out.begin(), out.end(), 0.0f);
        const size_t n = bank->beginBlock(N);
        for (size_t i = 0; i < n; ++i) {
            const float x = (b == 0 && i == 0) ? 1.0f : 0.0f;
            (void)bank->processVoiceSample(0u, i, x); // Dry is the caller's to pan; only the send matters here
            (void)bank->processVoiceSample(1u, i, x);
        }
        bank->endBlock(out, n);
        for (const float v : out) {
            ASSERT_TRUE(std::isfinite(v));
            wetEnergy += static_cast<double>(v) * v;
        }
    }
    EXPECT_GT(wetEnergy, 0.0);
}

TEST(AcousticVoiceBank, ZeroSendsGiveADryOutput) {
    auto bank = makeBank();
    ASSERT_TRUE(bank->post(AcousticCommand::environment(cave(), true)));
    (void)bank->drainCommands();
    for (uint32_t v = 0; v < AcousticVoiceBank::MAX_VOICES; ++v) {
        ASSERT_TRUE(bank->startVoice(v, v + 1u, {1.0f, 20000.0f}, 0.0f));
    }
    std::vector<float> out(2 * N);
    for (int b = 0; b < 6; ++b) {
        std::fill(out.begin(), out.end(), 0.0f);
        const size_t n = bank->beginBlock(N);
        for (uint32_t v = 0; v < AcousticVoiceBank::MAX_VOICES; ++v) {
            for (size_t i = 0; i < n; ++i) {
                (void)bank->processVoiceSample(v, i, 0.5f);
            }
        }
        bank->endBlock(out, n);
        ASSERT_TRUE(std::all_of(out.begin(), out.end(), [](float s) { return s == 0.0f; }));
    }
}

TEST(AcousticVoiceBank, DryOutputMatchesAStandaloneVoiceFilter) {
    auto bank = makeBank();
    VoiceAcousticFilter reference;
    const OcclusionTarget t{0.4f, 900.0f};
    ASSERT_TRUE(bank->startVoice(6u, 7u, t, 0.2f));
    reference.setOcclusionTarget(t);
    reference.setReverbSend(0.2f);
    reference.snapToTargets();
    reference.reset();
    (void)bank->beginBlock(N);
    for (size_t i = 0; i < N; ++i) {
        const float x = std::sin(0.05f * static_cast<float>(i));
        ASSERT_FLOAT_EQ(bank->processVoiceSample(6u, i, x), reference.process(x).dry) << i;
    }
}

// --- Voice handles: producer-assigned ids bound to mixer slots on the audio thread -----------------

TEST(VoiceHandleAllocator, HandlesAreUniqueAndNeverInvalid) {
    VoiceHandleAllocator alloc;
    std::vector<uint32_t> seen;
    for (int i = 0; i < 1000; ++i) {
        const uint32_t h = alloc.next();
        ASSERT_NE(h, INVALID_VOICE_HANDLE);
        seen.push_back(h);
    }
    std::sort(seen.begin(), seen.end());
    EXPECT_EQ(std::adjacent_find(seen.begin(), seen.end()), seen.end());
}

TEST(VoiceHandleAllocator, WrapAroundSkipsTheInvalidHandle) {
    VoiceHandleAllocator alloc(std::numeric_limits<uint32_t>::max());
    EXPECT_EQ(alloc.next(), std::numeric_limits<uint32_t>::max());
    EXPECT_EQ(alloc.next(), 1u);
    EXPECT_EQ(alloc.next(), 2u);
}

TEST(VoiceHandleAllocator, ConcurrentProducersNeverShareAHandle) {
    VoiceHandleAllocator alloc;
    constexpr int THREADS = 4;
    constexpr int PER_THREAD = 5000;
    std::vector<std::vector<uint32_t>> got(THREADS);
    std::vector<std::thread> threads;
    for (int t = 0; t < THREADS; ++t) {
        threads.emplace_back([&, t] {
            for (int i = 0; i < PER_THREAD; ++i) {
                got[static_cast<size_t>(t)].push_back(alloc.next());
            }
        });
    }
    for (std::thread& t : threads) {
        t.join();
    }
    std::vector<uint32_t> all;
    for (const auto& g : got) {
        all.insert(all.end(), g.begin(), g.end());
    }
    std::sort(all.begin(), all.end());
    EXPECT_EQ(std::adjacent_find(all.begin(), all.end()), all.end());
    EXPECT_EQ(all.size(), static_cast<size_t>(THREADS * PER_THREAD));
}

TEST(AcousticVoiceBankHandles, StartVoiceBindsTheHandleToTheMixerSlot) {
    auto bank = makeBank();
    EXPECT_TRUE(bank->startVoice(5u, 77u, {1.0f, 20000.0f}));
    EXPECT_EQ(bank->handleAt(5u), 77u);
    ASSERT_TRUE(bank->slotOf(77u).has_value());
    EXPECT_EQ(*bank->slotOf(77u), 5u);
    EXPECT_FALSE(bank->slotOf(78u).has_value());
    EXPECT_EQ(bank->handleAt(4u), INVALID_VOICE_HANDLE);
}

TEST(AcousticVoiceBankHandles, StartVoiceRejectsBadSlotOrInvalidHandle) {
    auto bank = makeBank();
    EXPECT_FALSE(bank->startVoice(AcousticVoiceBank::MAX_VOICES, 9u, {1.0f, 20000.0f}));
    EXPECT_FALSE(bank->startVoice(0u, INVALID_VOICE_HANDLE, {1.0f, 20000.0f}));
    EXPECT_FALSE(bank->slotOf(9u).has_value());
    EXPECT_FALSE(bank->slotOf(INVALID_VOICE_HANDLE).has_value());
}

TEST(AcousticVoiceBankHandles, OcclusionCommandReachesTheSlotBoundToItsHandle) {
    auto bank = makeBank();
    ASSERT_TRUE(bank->startVoice(9u, 1234u, {1.0f, 20000.0f}));
    // The handle is not a slot index: slot 1234 does not exist, slot 9 holds the sound.
    ASSERT_TRUE(bank->post(AcousticCommand::occlusion(1234u, {0.5f, 1000.0f})));
    EXPECT_EQ(bank->drainCommands(), 1u);
    for (int i = 0; i < 44100; ++i) {
        (void)bank->processVoiceSample(9u, 0, 0.0f);
    }
    EXPECT_NEAR(bank->voice(9u).currentGain(), 0.5f, 1e-4f);
    EXPECT_EQ(bank->rejectedCommands(), 0u);
}

TEST(AcousticVoiceBankHandles, StaleOcclusionForAReusedSlotIsDropped) {
    // Source A (behind a wall) ends; the audio thread gives its slot to unoccluded sound B. A late
    // 10 Hz sample still addressed to A must not muffle B.
    auto bank = makeBank();
    constexpr uint32_t A = 10u;
    constexpr uint32_t B = 11u;
    ASSERT_TRUE(bank->startVoice(3u, A, {0.26f, 1500.0f}));
    ASSERT_TRUE(bank->startVoice(3u, B, {1.0f, 20000.0f})); // Reuse / steal
    ASSERT_TRUE(bank->post(AcousticCommand::occlusion(A, {0.26f, 1500.0f})));
    ASSERT_TRUE(bank->post(AcousticCommand::reverbSend(A, 1.0f)));
    EXPECT_EQ(bank->drainCommands(), 0u);
    for (int i = 0; i < 4410; ++i) {
        (void)bank->processVoiceSample(3u, 0, 0.0f);
    }
    EXPECT_FLOAT_EQ(bank->voice(3u).currentGain(), 1.0f);
    EXPECT_FLOAT_EQ(bank->voice(3u).currentCoefficient(), onePoleCoefficient(20000.0f, DEFAULT_SAMPLE_RATE));
    EXPECT_EQ(bank->staleCommands(), 2u);
    EXPECT_FALSE(bank->slotOf(A).has_value());
}

TEST(AcousticVoiceBankHandles, StoppedVoiceIgnoresLateCommands) {
    auto bank = makeBank();
    ASSERT_TRUE(bank->startVoice(2u, 50u, {1.0f, 20000.0f}));
    bank->stopVoice(2u);
    EXPECT_EQ(bank->handleAt(2u), INVALID_VOICE_HANDLE);
    ASSERT_TRUE(bank->post(AcousticCommand::occlusion(50u, {0.1f, 400.0f})));
    EXPECT_EQ(bank->drainCommands(), 0u);
    EXPECT_EQ(bank->staleCommands(), 1u);
    EXPECT_FLOAT_EQ(bank->voice(2u).currentGain(), 1.0f);
}

TEST(AcousticVoiceBankHandles, InvalidHandleNeverMatchesAFreeSlot) {
    auto bank = makeBank(); // Every slot is free (bound to INVALID_VOICE_HANDLE)
    ASSERT_TRUE(bank->post(AcousticCommand::occlusion(INVALID_VOICE_HANDLE, {0.1f, 400.0f})));
    EXPECT_EQ(bank->drainCommands(), 0u);
    EXPECT_EQ(bank->rejectedCommands(), 1u);
    EXPECT_FLOAT_EQ(bank->voice(0u).currentGain(), 1.0f);
}

TEST(AcousticVoiceBankHandles, RestartingAHandleInAnotherSlotMovesTheBinding) {
    auto bank = makeBank();
    ASSERT_TRUE(bank->startVoice(1u, 5u, {1.0f, 20000.0f}));
    ASSERT_TRUE(bank->startVoice(7u, 5u, {1.0f, 20000.0f}));
    EXPECT_EQ(bank->handleAt(1u), INVALID_VOICE_HANDLE);
    ASSERT_TRUE(bank->slotOf(5u).has_value());
    EXPECT_EQ(*bank->slotOf(5u), 7u);
}

TEST(AcousticVoiceBankHandles, StartVoiceSnapsToInitialStateAndClearsFilterMemory) {
    // Called by the mixer in the same place it starts the PCM, so the first block of a new sound
    // never inherits the previous occupant's filter state, gain or send.
    auto bank = makeBank();
    ASSERT_TRUE(bank->startVoice(1u, 3u, {1.0f, 20000.0f}, 0.0f));
    (void)bank->beginBlock(N);
    for (int i = 0; i < 200; ++i) {
        (void)bank->processVoiceSample(1u, 0, 1.0f);
    }
    ASSERT_TRUE(bank->startVoice(1u, 4u, {0.25f, 600.0f}, 0.0f));
    EXPECT_FLOAT_EQ(bank->voice(1u).currentGain(), 0.25f);
    EXPECT_FLOAT_EQ(bank->voice(1u).currentCoefficient(), onePoleCoefficient(600.0f, DEFAULT_SAMPLE_RATE));
    EXPECT_FLOAT_EQ(bank->processVoiceSample(1u, 0, 0.0f), 0.0f);
}

TEST(AcousticVoiceBank, QueueMatchesTheSpecCommandQueueCapacity) {
    // A 512-entry MPSC queue absorbs bursts (e.g. 50 TNT at once). Whether acoustic
    // commands ride the engine's own queue (via apply()) or this one, the burst headroom is the same.
    EXPECT_EQ(AcousticVoiceBank::QUEUE_CAPACITY, 512u);
}

// --- Callbacks longer than the bus block (miniaudio may deliver 1024+ frames) ---------------------

namespace {

// Deterministic per-frame test signal for voice `v` at absolute frame `f`.
float signalAt(size_t v, size_t f) {
    return std::sin(0.013f * static_cast<float>(f) + static_cast<float>(v)) * ((f % 997u) < 3u ? 1.0f : 0.2f);
}

} // namespace

TEST(AcousticVoiceBankLongCallbacks, ProcessBlockMatchesExplicitSubBlocks) {
    constexpr size_t CALLBACK = 1024;
    auto bank = makeBank();
    auto reference = makeBank();
    for (AcousticVoiceBank* b : {bank.get(), reference.get()}) {
        ASSERT_TRUE(b->post(AcousticCommand::environment(cave(), true)));
        (void)b->drainCommands();
        ASSERT_TRUE(b->startVoice(0u, 1u, {0.8f, 5000.0f}, 0.5f));
        ASSERT_TRUE(b->startVoice(1u, 2u, {1.0f, 20000.0f}, 0.2f));
    }
    std::vector<float> out(2 * CALLBACK);
    std::vector<float> ref(2 * CALLBACK);
    double tailWet = 0.0;
    for (size_t cb = 0; cb < 6; ++cb) {
        std::fill(out.begin(), out.end(), 0.0f);
        std::fill(ref.begin(), ref.end(), 0.0f);
        const size_t base = cb * CALLBACK;
        bank->processBlock(out, CALLBACK, [&](std::span<float> sub, size_t offset, size_t frames) {
            for (size_t v = 0; v < 2; ++v) {
                for (size_t i = 0; i < frames; ++i) {
                    sub[2 * i] += bank->processVoiceSample(v, i, signalAt(v, base + offset + i));
                }
            }
        });
        for (size_t offset = 0; offset < CALLBACK; offset += N) {
            const size_t n = reference->beginBlock(N);
            ASSERT_EQ(n, N);
            std::span<float> sub(ref.data() + 2 * offset, 2 * n);
            for (size_t v = 0; v < 2; ++v) {
                for (size_t i = 0; i < n; ++i) {
                    sub[2 * i] += reference->processVoiceSample(v, i, signalAt(v, base + offset + i));
                }
            }
            reference->endBlock(sub, n);
        }
        for (size_t i = 0; i < out.size(); ++i) {
            ASSERT_FLOAT_EQ(out[i], ref[i]) << "callback " << cb << " sample " << i;
        }
        for (size_t f = N; f < CALLBACK; ++f) {
            tailWet += static_cast<double>(out[2 * f + 1]) * out[2 * f + 1]; // Right = wet only here
        }
    }
    EXPECT_GT(tailWet, 0.0) << "frames 512..1023 of each callback must carry reverb too";
    EXPECT_EQ(bank->droppedSendFrames(), 0u);
}

TEST(AcousticVoiceBankLongCallbacks, ProcessBlockClampsToTheOutputBuffer) {
    auto bank = makeBank();
    ASSERT_TRUE(bank->startVoice(0u, 1u, {1.0f, 20000.0f}));
    std::vector<float> out(2 * 700);
    size_t rendered = 0;
    bank->processBlock(out, 5000, [&](std::span<float> sub, size_t, size_t frames) {
        EXPECT_EQ(sub.size(), 2 * frames);
        EXPECT_LE(frames, AuxReverbBus::MAX_BLOCK_FRAMES);
        rendered += frames;
    });
    EXPECT_EQ(rendered, 700u);
}

TEST(AcousticVoiceBankLongCallbacks, SendsBeyondTheCurrentBlockAreCountedNotSilentlyLost) {
    // Misuse of the low-level API (one beginBlock for a 1024-frame callback) is made observable.
    auto bank = makeBank();
    ASSERT_TRUE(bank->startVoice(0u, 1u, {1.0f, 20000.0f}));
    const size_t n = bank->beginBlock(1024);
    ASSERT_EQ(n, N);
    for (size_t i = 0; i < 1024; ++i) {
        (void)bank->processVoiceSample(0u, i, 0.5f);
    }
    EXPECT_EQ(bank->droppedSendFrames(), 1024u - N);
}

TEST(AcousticVoiceBank, DefaultVoiceSendIsReverberatedWithoutAnEnvironmentCommand) {
    auto bank = makeBank();
    ASSERT_TRUE(bank->startVoice(0u, 1u, {1.0f, 20000.0f})); // Spec default send 0.15
    std::vector<float> out(2 * N);
    double wet = 0.0;
    for (int b = 0; b < 8; ++b) {
        std::fill(out.begin(), out.end(), 0.0f);
        bank->processBlock(out, N, [&](std::span<float>, size_t offset, size_t frames) {
            for (size_t i = 0; i < frames; ++i) {
                (void)bank->processVoiceSample(0u, i, (b == 0 && offset + i == 0) ? 1.0f : 0.0f);
            }
        });
        for (const float v : out) {
            wet += static_cast<double>(v) * v;
        }
    }
    EXPECT_GT(wet, 0.0);
}

TEST(AcousticCommand, EnvironmentCarriesTheDecayTime) {
    ReverbParams p = cave();
    p.decaySeconds = 2.5f;
    EXPECT_FLOAT_EQ(AcousticCommand::environment(p).decaySeconds, 2.5f);
}

TEST(AcousticVoiceBank, EnvironmentDecayIsHonouredAtA48kHzDevice) {
    auto bank = std::make_unique<AcousticVoiceBank>(48000.0f);
    ReverbParams p = cave();
    p.decaySeconds = 2.5f; // As produced by EnclosureDetector::reverbFor (feedback computed at 44.1 kHz)
    p.feedback = feedbackForDecay(2.5f);
    ASSERT_TRUE(bank->post(AcousticCommand::environment(p, true)));
    (void)bank->drainCommands();
    EXPECT_FLOAT_EQ(bank->bus().reverb().feedbackGain(), feedbackForDecay(2.5f, 48000.0f));
}
