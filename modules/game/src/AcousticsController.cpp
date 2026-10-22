#include "game/AcousticsController.hpp"

#include "world/Weather.hpp"

#include <array>
#include <cmath>

namespace betacraft::game {

RainAudibility measureRainAudibility(const world::World& world, const glm::vec3& listener) noexcept {
    static constexpr float d = kRainProbeRadius;
    static constexpr float diag = kRainProbeRadius * 0.75f;
    static constexpr std::array<glm::vec2, kRainProbeCount> kOffsets{{
        {0.0f, 0.0f}, {d, 0.0f}, {-d, 0.0f}, {0.0f, d}, {0.0f, -d},
        {diag, diag}, {diag, -diag}, {-diag, diag}, {-diag, -diag}
    }};
    if (!std::isfinite(listener.x) || !std::isfinite(listener.y) || !std::isfinite(listener.z)) {
        return {};
    }
    const acoustics::WorldChunkSolidityQuery query(world);
    float gainSum = 0.0f;
    float cutoffSum = 0.0f;
    for (const glm::vec2& offset : kOffsets) {
        const auto x = static_cast<int32_t>(std::floor(listener.x + offset.x));
        const auto z = static_cast<int32_t>(std::floor(listener.z + offset.y));
        if (world::precipitationAt(world.getBiomeAt(x, z)) != world::Precipitation::Rain) {
            continue;
        }
        const float rainY = std::max(static_cast<float>(world::precipitationHeight(world, x, z)) + 0.5f, listener.y);
        const glm::vec3 source(static_cast<float>(x) + 0.5f, rainY, static_cast<float>(z) + 0.5f);
        const acoustics::OcclusionTarget target =
            acoustics::occlusionTargetFor(acoustics::measureOcclusion(query, listener, source));
        gainSum += target.gain;
        cutoffSum += target.cutoffHz * target.gain;
    }
    RainAudibility out{};
    out.gain = gainSum / static_cast<float>(kRainProbeCount);
    if (gainSum > 0.0f) {
        out.cutoffHz = cutoffSum / gainSum;
    }
    return out;
}

AcousticsController::AcousticsController(audio::AudioEngine& audio) noexcept : m_audio(&audio) {}

void AcousticsController::update(double nowSeconds, const glm::vec3& listener, const acoustics::VoxelSolidityQuery& query) noexcept {
    m_now = nowSeconds;
    m_listener = listener;

    // The audio thread does not report finished voices; a source is dropped once its sound has
    // played out. A voice stolen earlier only makes its remaining updates stale, which the bank ignores.
    for (Tracked& tracked : m_tracked) {
        if (tracked.handle != acoustics::INVALID_VOICE_HANDLE && nowSeconds >= tracked.expiresAt) {
            (void)m_scheduler.removeSource(tracked.handle);
            tracked = Tracked{};
        }
    }

    for (const acoustics::OcclusionUpdate& update : m_scheduler.update(nowSeconds, listener, query)) {
        // Past R_max the voice keeps its last target; it is about to end anyway.
        if (!update.beyondAudibleRange) {
            post(acoustics::AcousticCommand::fromUpdate(update));
        }
    }

    if (const std::optional<acoustics::EnvironmentUpdate> environment = m_enclosure.update(nowSeconds, listener, query)) {
        post(acoustics::AcousticCommand::environment(environment->params, environment->snap));
    }
}

std::optional<audio::VoiceAcoustics> AcousticsController::acousticsAt(const glm::vec3& source,
                                                                      const acoustics::VoxelSolidityQuery& query) const noexcept {
    const acoustics::OcclusionSample sample = acoustics::measureOcclusion(query, m_listener, source);
    if (!sample.valid || sample.beyondAudibleRange) {
        return std::nullopt;
    }
    const acoustics::OcclusionTarget target = acoustics::occlusionTargetFor(sample, m_params);
    return audio::VoiceAcoustics{
        .occlusionGain = target.gain,
        .lpfCutoffHz = target.cutoffHz,
        .reverbSend = acoustics::VoiceAcousticFilter::DEFAULT_REVERB_SEND
    };
}

void AcousticsController::reset() noexcept {
    m_scheduler.clear();
    m_tracked.fill(Tracked{});
    m_enclosure.reset();
}

void AcousticsController::track(const audio::PlayedVoice& voice, const glm::vec3& source) noexcept {
    if (!voice.valid()) {
        return;
    }
    for (Tracked& tracked : m_tracked) {
        if (tracked.handle == acoustics::INVALID_VOICE_HANDLE) {
            // When the scheduler is full the voice simply keeps its initial occlusion.
            if (m_scheduler.addSource(voice.handle, source)) {
                tracked = Tracked{voice.handle, m_now + static_cast<double>(voice.durationSeconds) + TRACKING_GRACE_SECONDS};
            }
            return;
        }
    }
}

void AcousticsController::post(const acoustics::AcousticCommand& command) noexcept {
    if (!m_audio->postAcoustic(command)) {
        ++m_droppedCommands;
    }
}

} // namespace betacraft::game
