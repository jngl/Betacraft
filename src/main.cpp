#include "core/Config.hpp"
#include "core/Logger.hpp"
#include "game/Application.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string_view>

int main(int argc, char* argv[]) {
    betacraft::core::EngineConfig config{};
    int maxFrames = -1;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if (arg == "--help" || arg == "-h") {
            std::cout << "Minecraft Beta 1.7.3 Clone (C++20 / OpenGL 3.3 / SDL2 / miniaudio)\n"
                      << "Usage: betacraft [options]\n"
                      << "  --seed <uint64>            World generation seed (default: 173173173)\n"
                      << "  --render-distance <int>    Render distance in chunks (default: 7)\n"
                      << "  --save-dir <path>          Open this world at once (default: pick one on the title screen)\n"
                      << "  --fullscreen               Launch in fullscreen mode\n"
                      << "  --frames <N>               Run for N frames then exit (smoke test)\n"
                      << "  --upload-budget-ms <ms>    Per-frame chunk mesh upload budget (default: 4)\n"
                      << "  --gamemode <mode>          survival|creative, for new worlds only (default: survival)\n"
                      << "  --no-region-storage        Keep writing legacy chunks/ files (worlds without region/)\n"
                      << "  --no-async-gen             Generate chunks on the main thread\n"
                      << "  --no-pathfinding, --no-redstone, --no-weather   Disable those mechanics\n";
            return EXIT_SUCCESS;
        }
        if (arg == "--seed" && i + 1 < argc) {
            config.worldSeed = std::strtoull(argv[++i], nullptr, 10);
            config.autoEnterWorld = true;
        } else if (arg == "--render-distance" && i + 1 < argc) {
            config.renderDistanceChunks = std::clamp(std::atoi(argv[++i]), 2, 16);
        } else if (arg == "--save-dir" && i + 1 < argc) {
            config.worldSaveDir = argv[++i];
            config.autoEnterWorld = true;
        } else if (arg == "--fullscreen") {
            config.fullscreen = true;
        } else if (arg == "--frames" && i + 1 < argc) {
            maxFrames = std::atoi(argv[++i]);
            config.autoEnterWorld = true;
        } else if (arg == "--upload-budget-ms" && i + 1 < argc) {
            config.meshUploadBudgetMs = std::clamp(static_cast<float>(std::atof(argv[++i])), 0.0f, 100.0f);
        } else if (betacraft::core::parseFeatureFlagArg(arg, config.features)) {
            // Feature flag switched off (conventions §3)
        } else if (arg == "--gamemode" && i + 1 < argc) {
            const std::string_view value(argv[++i]);
            if (const auto mode = betacraft::core::parseGameMode(value)) {
                config.defaultGameMode = *mode;
            } else {
                betacraft::core::Logger::warn("Main", "Unknown game mode '", value, "', keeping the default.");
            }
        }
    }

    betacraft::game::Application app(config);
    if (!app.initialize()) {
        betacraft::core::Logger::error("Main", "Failed to initialize BetaCraft 1.7.3.");
        return EXIT_FAILURE;
    }

    return app.run(maxFrames);
}
