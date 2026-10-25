#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "core/Version.hpp"

namespace betacraft::core {

// Beta game modes (level.dat stores the number). Every cheat (palette, 64-block pick, fly,
// time skip, right-click piston) is gated on Player::isCreative().
enum class GameMode : uint8_t { Survival = 0, Creative = 1 };

// "survival" / "creative" (also "0" / "1"), case-insensitive; nullopt for anything else.
[[nodiscard]] inline std::optional<GameMode> parseGameMode(std::string_view text) noexcept {
    const auto equalsIgnoreCase = [text](std::string_view word) noexcept {
        if (text.size() != word.size()) return false;
        for (size_t i = 0; i < text.size(); ++i) {
            char c = text[i];
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
            if (c != word[i]) return false;
        }
        return true;
    };
    if (equalsIgnoreCase("survival") || text == "0") return GameMode::Survival;
    if (equalsIgnoreCase("creative") || text == "1") return GameMode::Creative;
    return std::nullopt;
}

// Risky mechanics behind switches (CLAUDE.md conventions), each exposed on the command line
// (--no-async-gen, --no-region-storage, ...) and removed one version after it stabilises.
struct FeatureFlags {
    bool asyncWorldGen{true};
    bool regionStorage{true}; // chunk bundles in region/ (off: legacy chunks/ files)
    bool mobPathfinding{true};
    bool redstone{true};
    bool weather{true};
};

// Turns the --no-<flag> switch `arg` off in `flags`; false when `arg` is not one of them.
[[nodiscard]] inline bool parseFeatureFlagArg(std::string_view arg, FeatureFlags& flags) noexcept {
    if (arg == "--no-async-gen") flags.asyncWorldGen = false;
    else if (arg == "--no-region-storage") flags.regionStorage = false;
    else if (arg == "--no-pathfinding") flags.mobPathfinding = false;
    else if (arg == "--no-redstone") flags.redstone = false;
    else if (arg == "--no-weather") flags.weather = false;
    else return false;
    return true;
}

// A world made on the Create World screen: display name, seed and game mode.
struct NewWorldParams {
    std::string name;
    uint64_t seed{0};
    GameMode mode{GameMode::Survival};
};

struct EngineConfig {
    // Window settings
    std::string windowTitle{kGameTitle};
    int32_t windowWidth{1280};
    int32_t windowHeight{720};
    bool vsync{true};
    bool fullscreen{false};

    // World & chunk constants
    static constexpr int32_t CHUNK_WIDTH = 16;
    static constexpr int32_t CHUNK_HEIGHT = 128;
    static constexpr int32_t CHUNK_DEPTH = 16;
    static constexpr int32_t CHUNK_VOLUME = CHUNK_WIDTH * CHUNK_HEIGHT * CHUNK_DEPTH;
    static constexpr int32_t SEA_LEVEL = 64;
    // Beta's fixed game tick (fluids, scheduled block updates), independent of physics steps.
    static constexpr int32_t GAME_TICKS_PER_SECOND = 20;
    static constexpr float GAME_TICK_SECONDS = 1.0f / static_cast<float>(GAME_TICKS_PER_SECOND);

    // Rendering settings
    int32_t renderDistanceChunks{7};
    float fovDegrees{70.0f};
    float nearPlane{0.08f};
    float farPlane{256.0f};
    bool smoothLighting{true};
    // Per-frame GPU upload budget for finished section meshes (4 ms by default). At least
    // one mesh is uploaded every frame, however small the budget.
    float meshUploadBudgetMs{4.0f};

    // Gameplay / Physics constants (Beta 1.7.3 feel)
    static constexpr float GRAVITY = 28.0f;
    static constexpr float TERMINAL_VELOCITY = 55.0f;
    static constexpr float JUMP_VELOCITY = 8.55f;
    static constexpr float WALK_SPEED = 4.317f;
    static constexpr float SPRINT_SPEED = 5.612f;
    static constexpr float SNEAK_SPEED = 1.35f;
    static constexpr float FLY_SPEED = 11.5f;
    static constexpr float SWIM_SPEED = 2.6f;

    static constexpr float PLAYER_WIDTH = 0.6f;
    static constexpr float PLAYER_HEIGHT = 1.8f;
    static constexpr float PLAYER_EYE_HEIGHT = 1.62f;
    static constexpr float PLAYER_SNEAK_EYE_HEIGHT = 1.48f;
    static constexpr float REACH_DISTANCE = 4.5f;

    // Day/Night cycle
    float dayLengthSeconds{1200.0f}; // Beta: 20 minutes (24000 ticks) full cycle (can be sped up with T/Y keys)

    // World persistence
    std::string worldSaveDir{"saves/world1"};
    uint64_t worldSeed{173173173ULL};
    // Mode of a freshly created world (--gamemode); existing worlds keep their saved mode.
    GameMode defaultGameMode{GameMode::Survival};
    // Initialize() opens worldSaveDir at once (main sets it for --save-dir, --seed and
    // --frames; the E2E tests too). Otherwise the game starts on the title screen with no
    // world and worlds are picked from savesRoot on the World Select screen.
    bool autoEnterWorld{false};
    std::string savesRoot{"saves"};

    FeatureFlags features{};
};

} // namespace betacraft::core
