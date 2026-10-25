#pragma once

#include "acoustics/AcousticCommand.hpp"
#include "acoustics/EnclosureMonitor.hpp"
#include "acoustics/OcclusionScheduler.hpp"
#include "acoustics/WorldSolidity.hpp"
#include "audio/AudioEngine.hpp"
#include "world/World.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

namespace betacraft::game {

// How much of the rain around a listener reaches it: the mean occlusion gain of
// kRainProbeCount columns around (and including) the listener's, each heard where the rain
// falls through them (the listener's height, or just above a roof that is higher). Columns
// without rain (snow, desert) count as silent. `cutoffHz` is the gain-weighted occlusion
// cutoff of the rainy columns (the minimum when none is heard).
struct RainAudibility {
    float gain{0.0f};
    float cutoffHz{acoustics::OcclusionParams{}.minCutoffHz};
};
inline constexpr int32_t kRainProbeCount = 9;
inline constexpr float kRainProbeRadius = 4.0f;
[[nodiscard]] RainAudibility measureRainAudibility(const world::World& world, const glm::vec3& listener) noexcept;

// Game-thread half of the acoustic layer. Every world sound starts with an occlusion target
// ray-marched from the listener at play time; while it plays, the OcclusionScheduler re-evaluates it
// at 10 Hz and the EnclosureMonitor tracks the listener's surroundings. Both post their results
// through AudioEngine's command queue to the AcousticVoiceBank in the mixer.
// World-backed overloads build a WorldChunkSolidityQuery per call: game thread only.
class AcousticsController : private core::NonCopyable {
public:
    static constexpr size_t MAX_TRACKED = acoustics::OcclusionScheduler::CAPACITY;
    // Keeps a source registered a little past its nominal end, so a voice delayed by one audio
    // period still gets its last occlusion update.
    static constexpr double TRACKING_GRACE_SECONDS = 0.1;

    explicit AcousticsController(audio::AudioEngine& audio) noexcept;

    // Once per frame: expires finished sources, runs the 10 Hz occlusion and enclosure updates and
    // posts what changed. `nowSeconds` is the game clock, also used for source lifetimes.
    void update(double nowSeconds, const glm::vec3& listener, const acoustics::VoxelSolidityQuery& query) noexcept;
    void update(double nowSeconds, const glm::vec3& listener, const world::World& world) noexcept {
        const acoustics::WorldChunkSolidityQuery query(world);
        update(nowSeconds, listener, query);
    }

    // Initial acoustics of a sound at `source` heard from the last listener; nullopt beyond R_max.
    [[nodiscard]] std::optional<audio::VoiceAcoustics> acousticsAt(const glm::vec3& source,
                                                                   const acoustics::VoxelSolidityQuery& query) const noexcept;

    // Starts a world sound at `source`: `play(const audio::VoiceAcoustics&) -> audio::PlayedVoice`
    // is called with its initial acoustics (or not at all when it is out of range), and the voice
    // it returns is tracked for 10 Hz occlusion updates until it ends.
    template <typename PlayFn>
    audio::PlayedVoice playAt(const glm::vec3& source, const acoustics::VoxelSolidityQuery& query, PlayFn&& play) {
        const std::optional<audio::VoiceAcoustics> initial = acousticsAt(source, query);
        if (!initial) {
            return {};
        }
        const audio::PlayedVoice voice = std::forward<PlayFn>(play)(*initial);
        track(voice, source);
        return voice;
    }
    template <typename PlayFn>
    audio::PlayedVoice playAt(const glm::vec3& source, const world::World& world, PlayFn&& play) {
        const acoustics::WorldChunkSolidityQuery query(world);
        return playAt(source, query, std::forward<PlayFn>(play));
    }

    // World change: forget every source and re-probe the surroundings (snapping) on the next update.
    void reset() noexcept;

    [[nodiscard]] size_t trackedSourceCount() const noexcept { return m_scheduler.sourceCount(); }
    [[nodiscard]] const glm::vec3& listener() const noexcept { return m_listener; }
    [[nodiscard]] uint64_t droppedCommands() const noexcept { return m_droppedCommands; }

private:
    struct Tracked {
        uint32_t handle{acoustics::INVALID_VOICE_HANDLE};
        double expiresAt{0.0};
    };

    void track(const audio::PlayedVoice& voice, const glm::vec3& source) noexcept;
    void post(const acoustics::AcousticCommand& command) noexcept;

    audio::AudioEngine* m_audio;
    acoustics::OcclusionScheduler m_scheduler;
    acoustics::EnclosureMonitor m_enclosure;
    acoustics::OcclusionParams m_params{};
    std::array<Tracked, MAX_TRACKED> m_tracked{};
    glm::vec3 m_listener{0.0f};
    double m_now{0.0};
    uint64_t m_droppedCommands{0};
};

} // namespace betacraft::game
