// P4.2 — end-to-end acoustic pipeline: world geometry -> 10 Hz DDA -> MPSC command -> voice filter
// + shared aux FDN -> stereo output. Mirrors what AudioEngine will do once integrated.
#include "test_support/AcousticsTestUtils.hpp"
#include "acoustics/AcousticVoiceBank.hpp"
#include "acoustics/EnclosureMonitor.hpp"
#include "acoustics/OcclusionScheduler.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <memory>
#include <numbers>
#include <random>
#include <vector>

namespace {

using namespace betacraft::acoustics;
using betacraft::acoustics::test::FakeVoxelGrid;

constexpr size_t BLOCK = AuxReverbBus::MAX_BLOCK_FRAMES;
const glm::vec3 LISTENER{0.5f, 0.5f, 0.5f};

struct Rendered {
    double dryEnergy{0.0};    // Of the voice's dry output
    double highEnergy{0.0};   // Of the first difference of the dry output (a crude high-pass)
    double wetEnergy{0.0};    // Of the bus output (reverb only: dry is not mixed in here)
};

// Plays white noise from `source` for `seconds`, running the game-thread side at 60 fps and the audio
// side in 512-frame blocks, exactly as the integrated engine would.
Rendered render(const FakeVoxelGrid& world, const glm::vec3& source, double seconds) {
    auto bank = std::make_unique<AcousticVoiceBank>();
    OcclusionScheduler scheduler;
    EnclosureMonitor monitor;
    VoiceHandleAllocator handles;
    const uint32_t handle = handles.next();
    constexpr size_t SLOT = 0; // Chosen by the mixer, unknown to the game thread
    EXPECT_TRUE(scheduler.addSource(handle, source));

    std::mt19937 rng(1u);
    std::uniform_real_distribution<float> noise(-1.0f, 1.0f);
    std::vector<float> out(2 * BLOCK);
    Rendered r;
    float prev = 0.0f;
    bool started = false;
    const auto blocks = static_cast<size_t>(seconds * DEFAULT_SAMPLE_RATE / BLOCK);
    for (size_t b = 0; b < blocks; ++b) {
        const double now = static_cast<double>(b * BLOCK) / DEFAULT_SAMPLE_RATE;
        for (const OcclusionUpdate& u : scheduler.update(now, LISTENER, world)) {
            if (started) {
                EXPECT_TRUE(bank->post(AcousticCommand::fromUpdate(u)));
            } else {
                // The play command carries the first occlusion sample; the mixer binds the slot.
                EXPECT_TRUE(bank->startVoice(SLOT, u.voiceHandle, {u.gain, u.cutoffHz}, 0.3f));
                started = true;
            }
        }
        if (const auto env = monitor.update(now, LISTENER, world)) {
            EXPECT_TRUE(bank->post(AcousticCommand::environment(env->params, env->snap)));
        }
        (void)bank->drainCommands();

        std::fill(out.begin(), out.end(), 0.0f);
        const size_t n = bank->beginBlock(BLOCK);
        for (size_t i = 0; i < n; ++i) {
            const float dry = bank->processVoiceSample(SLOT, i, noise(rng));
            if (b * BLOCK > 4410) { // Skip the first 100 ms
                r.dryEnergy += static_cast<double>(dry) * dry;
                r.highEnergy += static_cast<double>(dry - prev) * (dry - prev);
            }
            prev = dry;
        }
        bank->endBlock(out, n);
        for (const float v : out) {
            EXPECT_TRUE(std::isfinite(v));
            r.wetEnergy += static_cast<double>(v) * v;
        }
    }
    return r;
}

} // namespace

TEST(AcousticPipeline, StoneWallMufflesAndAttenuatesTheSource) {
    FakeVoxelGrid open;
    FakeVoxelGrid thin;
    thin.fillBox({3, -8, -8}, {5, 8, 8}); // 3 blocks of rock between listener and source
    FakeVoxelGrid thick;
    thick.fillBox({2, -8, -8}, {7, 8, 8}); // 6 blocks
    const glm::vec3 source{8.5f, 0.5f, 0.5f};
    const Rendered clear = render(open, source, 0.5);
    const Rendered muffled = render(thin, source, 0.5);
    const Rendered buried = render(thick, source, 0.5);
    // For white noise through y += a(x - y), E[(dy)^2] / E[y^2] = 2a, so this "brightness" ratio is a
    // direct readout of the LPF coefficient actually applied in the audio path.
    const auto brightness = [](const Rendered& r) { return r.highEnergy / r.dryEnergy; };
    const auto expectedA = [](float fc) { return static_cast<double>(onePoleCoefficient(fc, DEFAULT_SAMPLE_RATE)); };
    EXPECT_NEAR(brightness(clear), 2.0 * expectedA(20000.0f), 0.05);
    EXPECT_NEAR(brightness(muffled), 2.0 * expectedA(occlusionCutoffHz(3.0f)), 0.05);  // ~5.5 kHz
    EXPECT_NEAR(brightness(buried), 2.0 * expectedA(occlusionCutoffHz(6.0f)), 0.05);   // ~1.5 kHz
    // Gain follows thickness: 0.8^3 = 0.512 (energy ~0.26 before the LPF's own loss).
    EXPECT_LT(muffled.dryEnergy, 0.3 * clear.dryEnergy);
    EXPECT_LT(buried.dryEnergy, muffled.dryEnergy);
}

TEST(AcousticPipeline, CaveIsFarMoreReverberantThanOpenTerrain) {
    FakeVoxelGrid ground;
    ground.fillBox({-30, -1, -30}, {30, -1, 30});
    FakeVoxelGrid cave;
    cave.hollowBox({-10, -2, -10}, {10, 6, 10});
    const glm::vec3 source{4.5f, 0.5f, 0.5f}; // Same room, unoccluded in both cases
    const Rendered outside = render(ground, source, 0.5);
    const Rendered inside = render(cave, source, 0.5);
    EXPECT_NEAR(inside.dryEnergy, outside.dryEnergy, 1e-6 * outside.dryEnergy); // Same direct path
    EXPECT_GT(inside.wetEnergy, 10.0 * outside.wetEnergy);
}

TEST(AcousticPipeline, DdaRayCostIsBoundedEvenInDebugBuilds) {
    // The spec budget (< 0.5 us per ray) is asserted by DdaRayMeetsTheSpecBudgetInOptimisedBuilds in
    // NDEBUG builds. Debug + a hash-set query is far slower, so this only guards against complexity
    // blow-ups (e.g. allocation or O(n^2) per ray).
    FakeVoxelGrid grid;
    const auto start = std::chrono::steady_clock::now();
    uint32_t solid = 0;
    constexpr int RAYS = 4000;
    for (int i = 0; i < RAYS; ++i) {
        const float o = static_cast<float>(i % 7) * 0.1f;
        solid += measureOcclusion(grid, {0.5f + o, 0.5f, 0.5f}, {48.4f, 48.3f + o, 48.2f}).solidVoxels;
    }
    const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
    EXPECT_EQ(solid, 0u);
    EXPECT_LT(us / RAYS, 50.0) << "average " << us / RAYS << " us per ray";
}

#ifdef NDEBUG
namespace {

// Dense 128^3 bit grid (256 KB, cache-resident like the few chunk sections one ray touches) with an
// inline lookup: a realistic, cheap solidity source, so the timing measures the DDA, not a hash set.
class DenseGrid final : public VoxelSolidityQuery {
public:
    static constexpr int32_t SIZE = 128;
    explicit DenseGrid(uint32_t seed) : m_bits(static_cast<size_t>(SIZE) * SIZE * SIZE / 64u, 0u) {
        std::mt19937 rng(seed);
        std::uniform_int_distribution<int> d(0, 9);
        for (size_t i = 0; i < m_bits.size() * 64u; ++i) {
            if (d(rng) == 0) { // 10 % solid
                m_bits[i / 64u] |= uint64_t{1} << (i % 64u);
            }
        }
    }
    [[nodiscard]] bool isSolid(const betacraft::core::BlockPos& p) const noexcept override {
        const int32_t x = p.x + SIZE / 2;
        const int32_t z = p.z + SIZE / 2;
        if (x < 0 || x >= SIZE || z < 0 || z >= SIZE || p.y < 0 || p.y >= SIZE) {
            return false;
        }
        const size_t i = (static_cast<size_t>(x) * SIZE + static_cast<size_t>(p.y)) * SIZE + static_cast<size_t>(z);
        return ((m_bits[i / 64u] >> (i % 64u)) & 1u) != 0;
    }

private:
    std::vector<uint64_t> m_bits;
};

// Best-of-`reps` mean cost per ray (the minimum filters out preemption by other processes).
double bestMicrosPerRay(const VoxelSolidityQuery& q, const glm::vec3& listener, const std::vector<glm::vec3>& sources,
                        int reps, uint64_t& sink) {
    double best = std::numeric_limits<double>::infinity();
    for (int rep = 0; rep < reps; ++rep) {
        const auto start = std::chrono::steady_clock::now();
        for (const glm::vec3& s : sources) {
            sink += measureOcclusion(q, listener, s).solidVoxels;
        }
        const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
        best = std::min(best, us / static_cast<double>(sources.size()));
    }
    return best;
}

} // namespace
#endif

TEST(AcousticPipeline, DdaRayMeetsTheSpecBudgetInOptimisedBuilds) {
#ifndef NDEBUG
    GTEST_SKIP() << "timing budget only meaningful with optimisations (NDEBUG builds)";
#else
    // A wall-clock budget is a benchmark, not a correctness check: it only runs under the
    // ctest label "benchmark" (Phase2_acoustics_perf), which sets BETACRAFT_PERF_TESTS.
    if (std::getenv("BETACRAFT_PERF_TESTS") == nullptr) {
        GTEST_SKIP() << "set BETACRAFT_PERF_TESTS=1 (ctest -L benchmark) to run the timing budget";
    }
    const DenseGrid grid(11u);
    const glm::vec3 listener{0.5f, 64.5f, 0.5f};
    std::mt19937 rng(12u);
    std::uniform_real_distribution<float> u(-1.0f, 1.0f);
    std::vector<glm::vec3> fullRange; // Random directions at the edge of the audible range (47 blocks)
    std::vector<glm::vec3> worstCase; // 144-step rays (cube diagonals of the 48^3 box)
    for (int i = 0; i < 2000; ++i) {
        const glm::vec3 d = glm::normalize(glm::vec3{u(rng), u(rng), u(rng)});
        fullRange.push_back(listener + d * (static_cast<float>(MAX_AUDIBLE_RANGE) - 1.0f));
        const glm::vec3 diag{(i & 1) ? 47.9f : -47.9f, (i & 2) ? 47.7f : -47.7f, (i & 4) ? 47.6f : -47.6f};
        worstCase.push_back(listener + diag * (0.6f + 0.4f * std::fabs(u(rng))));
    }
    uint64_t sink = 0;
    const double rangeUs = bestMicrosPerRay(grid, listener, fullRange, 9, sink);
    const double worstUs = bestMicrosPerRay(grid, listener, worstCase, 9, sink);
    EXPECT_GT(sink, 0u);
    // P4.2 / Fiche 5 §4: < 0.5 us per ray across the audible radius.
    EXPECT_LT(rangeUs, 0.5) << "mean " << rangeUs << " us per R_max ray";
    // Longest legal rays: guards against a 2x+ regression (measured ~0.6-0.9 us on a desktop CPU).
    EXPECT_LT(worstUs, 1.5) << "mean " << worstUs << " us per 144-step ray";
#endif
}
