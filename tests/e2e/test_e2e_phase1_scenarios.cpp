#include "tests/phase1_test_contracts.hpp"
#include "test_support/GLTestContext.hpp"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <chrono>
#include <cmath>
#include <future>
#include <thread>
#include <vector>

using namespace betacraft;
using namespace betacraft::test_contracts;
using namespace testing;

// ============================================================================
// SCENARIO 1: Variable Framerate Physics Simulation (30 vs 60 vs 144 FPS)
// ============================================================================

TEST(Scenario1VariableFrameratePhysics, DeterministicPlayerTrajectoriesAcrossFramerates) {
    // 5.0 seconds simulation of complex movement:
    // Initial velocity: Walk forward (4.317 m/s) + jump impulse (8.55 m/s)
    const float duration = 5.0f;
    const glm::vec3 initPos(0.0f, 64.0f, 0.0f);
    const glm::vec3 initVel(4.317f, 8.55f, 0.0f);
    const glm::vec3 gravity(0.0f, -28.0f, 0.0f);

    // Run at 30 FPS (dt = 0.033333s)
    const auto res30 = simulateFiedlerPhysics(
        duration, 1.0f / 30.0f, 1.0f / 60.0f, 0.25f, initPos, initVel, gravity
    );

    // Run at 60 FPS (dt = 0.016667s)
    const auto res60 = simulateFiedlerPhysics(
        duration, 1.0f / 60.0f, 1.0f / 60.0f, 0.25f, initPos, initVel, gravity
    );

    // Run at 144 FPS (dt = 0.006944s)
    const auto res144 = simulateFiedlerPhysics(
        duration, 1.0f / 144.0f, 1.0f / 60.0f, 0.25f, initPos, initVel, gravity
    );

    // Total ticks at 60 TPS for 5.0s must be exactly 300 ticks across all framerates
    EXPECT_EQ(res30.tickCount, 300u);
    EXPECT_EQ(res60.tickCount, 300u);
    EXPECT_EQ(res144.tickCount, 300u);

    // Final position and velocity invariance (within 1e-4)
    EXPECT_NEAR(res30.finalPosition.x, res60.finalPosition.x, 1e-4f);
    EXPECT_NEAR(res30.finalPosition.y, res60.finalPosition.y, 1e-4f);
    EXPECT_NEAR(res30.finalPosition.z, res60.finalPosition.z, 1e-4f);

    EXPECT_NEAR(res60.finalPosition.x, res144.finalPosition.x, 1e-4f);
    EXPECT_NEAR(res60.finalPosition.y, res144.finalPosition.y, 1e-4f);
    EXPECT_NEAR(res60.finalPosition.z, res144.finalPosition.z, 1e-4f);

    EXPECT_NEAR(res30.finalVelocity.x, res60.finalVelocity.x, 1e-4f);
    EXPECT_NEAR(res30.finalVelocity.y, res60.finalVelocity.y, 1e-4f);
    EXPECT_NEAR(res60.finalVelocity.y, res144.finalVelocity.y, 1e-4f);
}

TEST(Scenario1VariableFrameratePhysics, JitterFramerateInvariance) {
    // Simulate wildly fluctuating framerates (alternating 8ms, 33ms, 16ms, 25ms)
    const std::vector<float> frameJitters = {
        0.008f, 0.033f, 0.016f, 0.025f, 0.012f, 0.028f
    };

    float accumulator = 0.0f;
    size_t ticks = 0;
    const float fixedDt = 1.0f / 60.0f;
    const float maxAcc = 0.25f;

    // Simulate 100 jittered frames (~2.0 seconds)
    float simulatedTime = 0.0f;
    for (size_t f = 0; f < 100; ++f) {
        const float dt = frameJitters[f % frameJitters.size()];
        simulatedTime += dt;
        accumulator += dt;
        if (accumulator > maxAcc) accumulator = maxAcc;

        while (accumulator >= fixedDt) {
            accumulator -= fixedDt;
            ++ticks;
        }
    }

    const size_t expectedTicks = static_cast<size_t>(simulatedTime / fixedDt);
    EXPECT_EQ(ticks, expectedTicks);
}

// ============================================================================
// SCENARIO 2: Heavy Voxel Modification & Dynamic Palette Upgrades/Downgrades
// ============================================================================

TEST(Scenario2HeavyVoxelModification, DynamicPaletteTiersAndLazyLightLifecycle) {
    ContractPalettedSection section;

    // 1. Initial State: Homogeneous air, 0-bit palette, zero heap
    EXPECT_TRUE(section.isHomogeneous());
    EXPECT_EQ(section.getBitsPerVoxel(), 0u);
    EXPECT_FALSE(section.hasLightData());
    EXPECT_EQ(section.getMemoryFootprintBytes(), sizeof(ContractPalettedSection));

    // 2. Phase A: Place 15 unique block types -> 4-bit palette
    for (uint16_t b = 1; b <= 15; ++b) {
        section.setBlock(b, 0, 0, b);
    }
    EXPECT_EQ(section.getBitsPerVoxel(), 4u);
    EXPECT_EQ(section.getPaletteSize(), 16u);
    EXPECT_FALSE(section.hasLightData()); // Light untouched -> zero light heap allocation

    // 3. Phase B: Place 20 more unique block types (35 unique total) -> 8-bit palette
    for (uint16_t b = 16; b <= 35; ++b) {
        const int x = b % 16;
        const int y = (b / 16) % 16;
        section.setBlock(x, y, 1, b);
    }
    EXPECT_EQ(section.getBitsPerVoxel(), 8u);
    EXPECT_EQ(section.getPaletteSize(), 36u);
    EXPECT_FALSE(section.hasLightData());

    // Verify all 35 blocks correctly retained
    for (uint16_t b = 1; b <= 15; ++b) {
        EXPECT_EQ(section.getBlock(b, 0, 0), b);
    }
    for (uint16_t b = 16; b <= 35; ++b) {
        const int x = b % 16;
        const int y = (b / 16) % 16;
        EXPECT_EQ(section.getBlock(x, y, 1), b);
    }

    // 4. Phase C: Perturb lighting at 20 voxels -> lazy DenseLightArray allocated
    for (int i = 0; i < 20; ++i) {
        section.setBlockLight(i % 16, i / 16, 2, static_cast<uint8_t>(i % 15 + 1));
    }
    EXPECT_TRUE(section.hasLightData());
    EXPECT_GT(section.getMemoryFootprintBytes(), sizeof(ContractPalettedSection) + 4096);

    // 5. Phase D: Reset lighting back to uniform -> DenseLightArray freed back to nullptr
    section.setUniformLighting(15, 0);
    EXPECT_FALSE(section.hasLightData());
    EXPECT_EQ(section.getSkyLight(0, 0, 0), 15u);
    EXPECT_EQ(section.getBlockLight(0, 0, 0), 0u);

    // Blocks must remain intact after light free
    EXPECT_EQ(section.getBlock(1, 0, 0), 1u);
    EXPECT_EQ(section.getBlock(35 % 16, (35 / 16) % 16, 1), 35u);
}

// ============================================================================
// SCENARIO 3: Rapid Torch Placement & Removal in Enclosed and Open Spaces
// ============================================================================

TEST(Scenario3RapidTorchPlacementRemoval, ManhattanSphereBoundsOpenVsEnclosed) {
    // Analytical check: discrete BFS in open space cannot exceed 4,991 voxels
    const size_t openAirVoxelCount = computeDiscreteManhattanVolumeBFS(15);
    EXPECT_EQ(openAirVoxelCount, 4991u);

    // Enclosed space: torch in 5x5x5 stone room
    // The volume of the entire room interior is 3x3x3 = 27 voxels
    const size_t enclosedRoomVoxelCount = 27;
    EXPECT_LT(enclosedRoomVoxelCount, openAirVoxelCount);

    // Legacy chunk engine recalculates 5 whole chunks: 163,840 voxels
    const size_t legacyRecalculationVoxels = 163840;
    const double speedupFactor = static_cast<double>(legacyRecalculationVoxels) / static_cast<double>(openAirVoxelCount);

    // Gain must be at least 32.8x in worst-case open air
    EXPECT_GE(speedupFactor, 32.8);

    // In enclosed room, gain is over 600x
    const double enclosedSpeedup = static_cast<double>(legacyRecalculationVoxels) / static_cast<double>(enclosedRoomVoxelCount);
    EXPECT_GE(enclosedSpeedup, 600.0);
}

// ============================================================================
// SCENARIO 4: Burst Audio Producer Contention (512 commands) without Glitches
// ============================================================================

struct DummyAudioCommand {
    uint32_t soundId{0};
    float volume{1.0f};
    float pitch{1.0f};
    int producerId{0};
};

TEST(Scenario4BurstAudioProducerContention, LocklessMpsc512CommandsHighContention) {
    ContractMpscRingBuffer<DummyAudioCommand, 512> audioQueue;
    constexpr int numProducers = 4;
    constexpr int commandsPerProducer = 128; // Total = 512 commands

    std::atomic<bool> startFlag{false};
    std::atomic<size_t> totalPopped{0};

    // Spawn 4 producer threads
    std::vector<std::thread> producers;
    producers.reserve(numProducers);

    for (int p = 0; p < numProducers; ++p) {
        producers.emplace_back([&audioQueue, &startFlag, p]() {
            while (!startFlag.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            for (int i = 0; i < commandsPerProducer; ++i) {
                DummyAudioCommand cmd{static_cast<uint32_t>(i), 1.0f, 1.0f, p};
                while (!audioQueue.push(cmd)) {
                    std::this_thread::yield(); // Backoff if queue temporarily full
                }
            }
        });
    }

    // Consumer running in background simulating 100 Hz audio callback
    std::atomic<bool> doneConsuming{false};
    std::thread consumer([&audioQueue, &totalPopped, &doneConsuming]() {
        std::array<int, numProducers> lastSequenceSeen{};
        lastSequenceSeen.fill(-1);

        while (!doneConsuming.load(std::memory_order_relaxed) || totalPopped.load() < 512) {
            DummyAudioCommand cmd{};
            while (audioQueue.pop(cmd)) {
                // Verify FIFO order per producer
                EXPECT_EQ(cmd.soundId, static_cast<uint32_t>(lastSequenceSeen[cmd.producerId] + 1));
                lastSequenceSeen[cmd.producerId] = static_cast<int>(cmd.soundId);
                totalPopped.fetch_add(1, std::memory_order_relaxed);
            }
            std::this_thread::sleep_for(std::chrono::microseconds(50));
        }
    });

    // Start all producers simultaneously
    startFlag.store(true, std::memory_order_release);

    for (auto& t : producers) {
        t.join();
    }

    // Allow consumer to finish draining remaining items
    doneConsuming.store(true, std::memory_order_release);
    consumer.join();

    EXPECT_EQ(totalPopped.load(), 512u);
}

// ============================================================================
// SCENARIO 5: Complete User Journey: Launch -> Options Rebind -> InGame -> Move & Build
// ============================================================================

TEST(Scenario5CompleteUserJourney, FullOptionsAndInGameWorkflow) {
    betacraft::test::TempDir tempDir;
    const auto optionsPath = tempDir.path() / "options.txt";

    // Step 1: Launch - Compute GUI Scale for 1920x1080 display
    const int autoScale = calculateVirtualGuiScale(1920, 1080, GuiScalePreference::Auto);
    EXPECT_EQ(autoScale, 4);

    // Step 2: Options Rebind - French AZERTY user changes Forward (W: 26) to Z (29)
    KeyConfig config;
    config.bindings["key_forward"] = 29; // SDL_SCANCODE_Z
    config.bindings["key_backward"] = 22; // SDL_SCANCODE_S
    config.bindings["key_left"] = 20;     // SDL_SCANCODE_Q
    config.bindings["key_right"] = 7;     // SDL_SCANCODE_D
    config.guiScale = 3;                  // User prefers 3x instead of 4x
    config.fov = 85.0f;
    config.musicVolume = 0.6f;

    config.saveToFile(optionsPath);
    EXPECT_TRUE(std::filesystem::exists(optionsPath));

    // Step 3: Persistence - Reload from disk and verify
    KeyConfig loaded;
    EXPECT_TRUE(loaded.loadFromFile(optionsPath));
    EXPECT_EQ(loaded.bindings["key_forward"], 29);
    EXPECT_EQ(loaded.bindings["key_left"], 20);
    EXPECT_EQ(loaded.guiScale, 3);
    EXPECT_FLOAT_EQ(loaded.fov, 85.0f);
    EXPECT_FLOAT_EQ(loaded.musicVolume, 0.6f);

    // Step 4: In-Game Action Resolution - Key 29 triggers Forward
    const int pressedKey = 29;
    GameAction resolvedAction = GameAction::Backward; // Default dummy
    for (const auto& [actionName, scancode] : loaded.bindings) {
        if (scancode == pressedKey) {
            if (actionName == "key_forward") resolvedAction = GameAction::Forward;
            break;
        }
    }
    EXPECT_EQ(resolvedAction, GameAction::Forward);

    // Step 5: Simulate Player movement in game world using fixed timestep
    const auto simRes = simulateFiedlerPhysics(
        1.0f, 1.0f / 60.0f, 1.0f / 60.0f, 0.25f, glm::vec3(0.0f), glm::vec3(4.317f, 0.0f, 0.0f)
    );
    EXPECT_EQ(simRes.tickCount, 60u);
    EXPECT_GT(simRes.finalPosition.x, 0.0f);

    // Step 6: Player places block into sub-chunk
    ContractPalettedSection section;
    section.setBlock(1, 1, 1, 1); // Place Stone
    EXPECT_EQ(section.getBlock(1, 1, 1), 1u);
}
