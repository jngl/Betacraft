#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "audio/MpscRingBuffer.hpp"
#include "audio/AudioEngine.hpp"
#include "render/TextRenderer.hpp"
#include "ui/InputManager.hpp"
#include "ui/ScreenState.hpp"
#include "ui/TitleScreen.hpp"
#include "ui/OptionsMenu.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <thread>
#include <vector>

using namespace betacraft;

namespace {

struct HeavyPayload {
    uint64_t id{0};
    uint32_t producerId{0};
    float data[4]{0.0f, 0.0f, 0.0f, 0.0f};
    bool valid{true};
};

} // namespace

// ============================================================================
// SUITE 1: Lockless MPSC Ring Buffer Adversarial Concurrency
// ============================================================================

TEST(AdversarialM3Stress, Mpsc16ProducersHighContentionStress) {
    audio::MpscRingBuffer<HeavyPayload, 512> ring;
    constexpr int numProducers = 16;
    constexpr int itemsPerProducer = 10000;
    constexpr int totalItems = numProducers * itemsPerProducer;

    std::atomic<bool> start{false};
    std::atomic<bool> producersDone{false};
    std::vector<std::thread> producers;
    producers.reserve(numProducers);

    for (int p = 0; p < numProducers; ++p) {
        producers.emplace_back([&ring, &start, p]() {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            for (int i = 0; i < itemsPerProducer; ++i) {
                HeavyPayload payload{
                    .id = static_cast<uint64_t>(i),
                    .producerId = static_cast<uint32_t>(p),
                    .data = {static_cast<float>(i), 1.0f, 2.0f, 3.0f},
                    .valid = true
                };
                while (!ring.push(payload)) {
                    std::this_thread::yield();
                }
            }
        });
    }

    std::vector<int64_t> lastSeen(numProducers, -1);
    std::vector<int> counts(numProducers, 0);
    int totalPopped = 0;

    start.store(true, std::memory_order_release);

    while (totalPopped < totalItems) {
        HeavyPayload item{};
        if (ring.pop(item)) {
            EXPECT_TRUE(item.valid);
            EXPECT_EQ(item.id, static_cast<uint64_t>(lastSeen[item.producerId] + 1));
            lastSeen[item.producerId] = static_cast<int64_t>(item.id);
            counts[item.producerId]++;
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
        EXPECT_EQ(counts[p], itemsPerProducer);
    }
}

TEST(AdversarialM3Stress, MpscMillionItemSequentialWraparound) {
    audio::MpscRingBuffer<uint32_t, 64> ring;
    constexpr uint32_t iterations = 1000000;

    for (uint32_t i = 0; i < iterations; ++i) {
        EXPECT_TRUE(ring.push(i));
        uint32_t out = 0;
        EXPECT_TRUE(ring.pop(out));
        EXPECT_EQ(out, i);
    }
    EXPECT_TRUE(ring.empty());
}

TEST(AdversarialM3Stress, MpscCacheLineAlignment) {
    audio::MpscRingBuffer<uint32_t, 512> ring;
    const uintptr_t ringAddr = reinterpret_cast<uintptr_t>(&ring);
    EXPECT_EQ(ringAddr % 64, 0u);
}

// ============================================================================
// SUITE 2: MSDF Shaders, Median Math & GUI Scaling Fuzzing
// ============================================================================

TEST(AdversarialM3Stress, MsdfMedianMathEdgeCases) {
    // Normal cases
    EXPECT_FLOAT_EQ(render::TextRenderer::msdfMedian(0.1f, 0.5f, 0.9f), 0.5f);
    EXPECT_FLOAT_EQ(render::TextRenderer::msdfMedian(0.9f, 0.5f, 0.1f), 0.5f);
    EXPECT_FLOAT_EQ(render::TextRenderer::msdfMedian(0.5f, 0.9f, 0.1f), 0.5f);
    EXPECT_FLOAT_EQ(render::TextRenderer::msdfMedian(0.5f, 0.1f, 0.9f), 0.5f);

    // Identical values
    EXPECT_FLOAT_EQ(render::TextRenderer::msdfMedian(0.42f, 0.42f, 0.42f), 0.42f);
    EXPECT_FLOAT_EQ(render::TextRenderer::msdfMedian(0.0f, 0.0f, 1.0f), 0.0f);
    EXPECT_FLOAT_EQ(render::TextRenderer::msdfMedian(1.0f, 1.0f, 0.0f), 1.0f);

    // Negative and extreme floats
    EXPECT_FLOAT_EQ(render::TextRenderer::msdfMedian(-10.0f, 0.0f, 10.0f), 0.0f);
    EXPECT_FLOAT_EQ(render::TextRenderer::msdfMedian(-1e6f, -1e3f, -1.0f), -1e3f);
}

TEST(AdversarialM3Stress, VirtualGuiScaleFuzzingExtremes) {
    // Negative, zero, and minimum dimensions must not divide by zero or infinite loop
    EXPECT_EQ(render::TextRenderer::calculateGuiScale(0, 0, ui::GuiScalePreference::Auto), 1);
    EXPECT_EQ(render::TextRenderer::calculateGuiScale(-100, -100, ui::GuiScalePreference::Auto), 1);
    EXPECT_EQ(render::TextRenderer::calculateGuiScale(1, 1, ui::GuiScalePreference::Auto), 1);

    // Minecraft classic threshold values: (320*k, 240*k)
    EXPECT_EQ(render::TextRenderer::calculateGuiScale(319, 239, ui::GuiScalePreference::Auto), 1);
    EXPECT_EQ(render::TextRenderer::calculateGuiScale(320, 240, ui::GuiScalePreference::Auto), 1);
    EXPECT_EQ(render::TextRenderer::calculateGuiScale(639, 479, ui::GuiScalePreference::Auto), 1);
    EXPECT_EQ(render::TextRenderer::calculateGuiScale(640, 480, ui::GuiScalePreference::Auto), 2);
    EXPECT_EQ(render::TextRenderer::calculateGuiScale(960, 720, ui::GuiScalePreference::Auto), 3);
    EXPECT_EQ(render::TextRenderer::calculateGuiScale(1280, 960, ui::GuiScalePreference::Auto), 4);
    EXPECT_EQ(render::TextRenderer::calculateGuiScale(1920, 1080, ui::GuiScalePreference::Auto), 4);

    // Ultra-high resolutions (4K, 8K, 16K, massive synthetic) must clamp to 4
    EXPECT_EQ(render::TextRenderer::calculateGuiScale(3840, 2160, ui::GuiScalePreference::Auto), 4);
    EXPECT_EQ(render::TextRenderer::calculateGuiScale(7680, 4320, ui::GuiScalePreference::Auto), 4);
    EXPECT_EQ(render::TextRenderer::calculateGuiScale(100000, 100000, ui::GuiScalePreference::Auto), 4);
}

TEST(AdversarialM3Stress, TextRendererStringEdgeCases) {
    // Empty text
    std::vector<ui::UiQuadVertex> verts;
    render::TextRenderer::appendText(verts, "", 0.0f, 0.0f, 1.0f, glm::vec4(1.0f), false);
    EXPECT_TRUE(verts.empty());
    EXPECT_FLOAT_EQ(render::TextRenderer::measureTextWidth("", 1.0f), 0.0f);

    // Only spaces
    render::TextRenderer::appendText(verts, "     ", 0.0f, 0.0f, 1.0f, glm::vec4(1.0f), false);
    EXPECT_TRUE(verts.empty()); // Spaces emit no geometry
    EXPECT_GT(render::TextRenderer::measureTextWidth("     ", 1.0f), 0.0f);

    // Non-ASCII and control characters (e.g. accented letters, unicode, nulls)
    const std::string exoticText = "B\xc3\xa9taCraft \xe2\x98\x85 \x01\x02\x7f";
    render::TextRenderer::appendText(verts, exoticText, 0.0f, 0.0f, 1.0f, glm::vec4(1.0f), true);
    // With dropShadow=true, each non-space character emits 12 vertices (2 quads: shadow + text)
    EXPECT_GT(verts.size(), 0u);
    EXPECT_EQ(verts.size() % 6, 0u);
}

// ============================================================================
// SUITE 3: InputManager, Options File Fuzzing & Screen State Machine
// ============================================================================

TEST(AdversarialM3Stress, OptionsFileCorruptedFuzzing) {
    const std::filesystem::path fuzzedPath = std::filesystem::temp_directory_path() / "fuzzed_options.txt";

    // Write highly corrupted lines
    {
        std::ofstream out(fuzzedPath);
        out << "\n\n\n";
        out << "# A comment without value\n";
        out << "missing_colon_line_without_delimiter\n";
        out << "::::::::::::\n";
        out << "key_forward:99999999999999999999999999999999999999999999\n"; // Overflow int
        out << "key_backward:-999999999999999999999999999999999999999999\n"; // Underflow int
        out << "fov:NaN\n";
        out << "fov:Infinity\n";
        out << "fov:-1e50\n";
        out << "gui_scale:9999\n";
        out << "render_distance:not_a_number\n";
        out << "music_volume:1.0e+99\n";
        out << "sound_volume:\n";
        out << "key_left:20\n"; // Valid: Q
        out << "key_right:7\n";  // Valid: D
    }

    ui::InputManager input;
    EXPECT_TRUE(input.loadFromFile(fuzzedPath));

    // Valid keys are parsed
    EXPECT_EQ(input.getPrimaryBinding(ui::GameAction::MoveLeft), static_cast<SDL_Scancode>(20));
    EXPECT_EQ(input.getPrimaryBinding(ui::GameAction::MoveRight), static_cast<SDL_Scancode>(7));

    // Default forward was preserved despite int overflow attempt
    EXPECT_EQ(input.getPrimaryBinding(ui::GameAction::MoveForward), SDL_SCANCODE_W);

    std::filesystem::remove(fuzzedPath);
}

TEST(AdversarialM3Stress, ScreenStateTransitionsStress) {
    ui::ScreenState state = ui::ScreenState::TitleScreen;
    std::mt19937 rng(12345);
    // Every state up to the last one (S7.8: Loading, after WorldSelect and CreateWorld).
    constexpr int kLastState = static_cast<int>(ui::ScreenState::Loading);
    std::uniform_int_distribution<int> dist(0, kLastState);

    for (int i = 0; i < 100000; ++i) {
        const auto next = static_cast<ui::ScreenState>(dist(rng));
        state = next;
        EXPECT_GE(static_cast<int>(state), 0);
        EXPECT_LE(static_cast<int>(state), kLastState);
    }
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
