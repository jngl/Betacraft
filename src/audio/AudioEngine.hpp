#pragma once

#include "acoustics/AcousticCommand.hpp"
#include "acoustics/AcousticVoiceBank.hpp"
#include "core/MpscRingBuffer.hpp"
#include "core/Types.hpp"
#include "world/Block.hpp"
#include "miniaudio.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <random>
#include <span>
#include <vector>

namespace betacraft::audio {

enum class SoundEffect : uint8_t {
    StepStone = 0,
    StepGrass,
    StepDirt,
    StepWood,
    StepSand,
    StepCloth,
    BreakStone,
    BreakGrass,
    BreakWood,
    BreakGlass,
    WaterSplash,
    PistonExtend,
    UiClick,
    LavaFizz,
    Hurt,      // Beta damage.hurtflesh
    FallSmall, // Beta damage.fallsmall
    FallBig,   // Beta damage.fallbig (fall damage above 4)
    Pop,       // Beta random.pop (item pickup)
    Eat,       // Eating food
    ZombieGroan, // Beta mob.zombie (ambient)
    PigOink,     // Beta mob.pig
    SheepBaa,    // Beta mob.sheep
    MobHurt,     // A mob hit (melee, fire)
    MobDeath,    // A mob dies
    DoorOpen,    // Beta random.door_open (S5.6)
    DoorClose,   // Beta random.door_close
    Explosion,   // Beta random.explode (S6.6)
    Click,       // Beta random.click: levers, buttons, pressure plates (S8.7)
    Thunder,     // Beta ambient.weather.thunder (S8.12)
    Count
};

enum class AudioCommandType : uint8_t {
    PlaySound = 0,
    StopAll,
    Acoustic // Occlusion / reverb-send / environment update for the AcousticVoiceBank
};

// Initial acoustic state of a world sound, computed on the game thread when it starts (spec
// AudioCommand::lpfCutoffHz / reverbSend). The 10 Hz updates that follow travel as Acoustic commands.
struct VoiceAcoustics {
    float occlusionGain{1.0f};
    float lpfCutoffHz{acoustics::OcclusionParams{}.maxCutoffHz};
    float reverbSend{acoustics::VoiceAcousticFilter::DEFAULT_REVERB_SEND};
};

struct AudioCommand {
    AudioCommandType type{AudioCommandType::PlaySound};
    SoundEffect effect{SoundEffect::StepStone};
    float volume{1.0f};
    float pitch{1.0f};
    // PlaySound: producer-assigned handle that later Acoustic commands address. The invalid handle
    // marks a non-spatial sound (UI), mixed dry without occlusion or reverb.
    uint32_t voiceHandle{acoustics::INVALID_VOICE_HANDLE};
    float occlusionGain{1.0f};
    float lpfCutoffHz{acoustics::OcclusionParams{}.maxCutoffHz};
    float reverbSend{acoustics::VoiceAcousticFilter::DEFAULT_REVERB_SEND};
    acoustics::AcousticCommand acoustic{}; // Acoustic only
};

// What a play call started: the handle to track for occlusion updates, and how long the sound lasts.
struct PlayedVoice {
    uint32_t handle{acoustics::INVALID_VOICE_HANDLE};
    float durationSeconds{0.0f};

    [[nodiscard]] bool valid() const noexcept { return handle != acoustics::INVALID_VOICE_HANDLE; }
};

class AudioEngine : private core::NonCopyable {
public:
    static constexpr uint32_t SAMPLE_RATE = 44100;

    using Acoustics = std::optional<VoiceAcoustics>;

    AudioEngine();
    ~AudioEngine() noexcept;

    [[nodiscard]] bool initialize();
    void shutdown() noexcept;

    // Without `acoustics` a sound is non-spatial: dry, unoccluded, no reverb (and gets no handle).
    PlayedVoice playSound(SoundEffect effect, float volume, float pitch, const Acoustics& acoustics = std::nullopt);
    PlayedVoice playBlockStep(world::SoundCategory category, const Acoustics& acoustics = std::nullopt);
    PlayedVoice playBlockDigHit(world::SoundCategory category, const Acoustics& acoustics = std::nullopt);
    PlayedVoice playBlockBreak(world::SoundCategory category, const Acoustics& acoustics = std::nullopt);
    PlayedVoice playBlockPlace(world::SoundCategory category, const Acoustics& acoustics = std::nullopt);
    PlayedVoice playWaterSplash(const Acoustics& acoustics = std::nullopt);
    PlayedVoice playPistonSound(const Acoustics& acoustics = std::nullopt);
    PlayedVoice playLavaFizz(float volumeScale, const Acoustics& acoustics = std::nullopt);
    void playUiClick();
    void stopAll();

    // S8.12 one thunder clap, non-spatial (Beta plays it at volume 10000: heard everywhere);
    // `volume` 0..1, e.g. lower for a far strike.
    PlayedVoice playThunder(float volume);

    // S8.12 procedural rain loop (Beta ambient.weather.rain), mixed dry under the voices. The
    // game sets its gain (rainStrength x acoustic occlusion of the rain around the listener,
    // 0 = silent) and the low-pass cutoff of that occlusion; the mixer glides to both within
    // ~0.25 s so changes never click. Any thread.
    void setRainLoop(float gain, float lpfCutoffHz = acoustics::OcclusionParams{}.maxCutoffHz) noexcept;
    [[nodiscard]] float rainLoopGain() const noexcept { return m_rainGain.load(std::memory_order_relaxed); }
    [[nodiscard]] float rainLoopCutoffHz() const noexcept { return m_rainCutoffHz.load(std::memory_order_relaxed); }
    // Gain the mixer has reached; audio-thread state, read it only while nothing is mixing (tests).
    [[nodiscard]] float rainLoopLevel() const noexcept { return m_rainLevel; }

    // Any thread: forwards an acoustic update through the command queue. False when the queue is full.
    bool postAcoustic(const acoustics::AcousticCommand& command) noexcept;

    // The audio callback's mixing path, for headless use (tests, no device): mixes `frameCount`
    // interleaved stereo frames into `out`. Refused (false) while a device owns the callback, since
    // the queue and voices have a single consumer.
    [[nodiscard]] bool renderFrames(float* outInterleavedStereo, uint32_t frameCount) noexcept;
    [[nodiscard]] bool hasDevice() const noexcept { return m_deviceInitialized; }

    // Audio-thread state; read it only while nothing is mixing (tests).
    [[nodiscard]] const acoustics::AcousticVoiceBank& acousticBank() const noexcept { return *m_acoustics; }

    void toggleMusic() noexcept { m_musicEnabled.store(!m_musicEnabled.load(std::memory_order_relaxed), std::memory_order_relaxed); }
    [[nodiscard]] bool isMusicEnabled() const noexcept { return m_musicEnabled.load(std::memory_order_relaxed); }

    // The synthesised samples of `effect` (mono, SAMPLE_RATE), empty for Count. Immutable
    // after construction, so any thread may read them.
    [[nodiscard]] std::span<const float> soundBuffer(SoundEffect effect) const noexcept {
        const auto i = static_cast<size_t>(effect);
        return i < m_soundBuffers.size() ? std::span<const float>(m_soundBuffers[i]) : std::span<const float>{};
    }

    [[nodiscard]] const core::MpscRingBuffer<AudioCommand, 512>& getCommandQueue() const noexcept { return m_commandQueue; }

private:
    struct ActiveVoice {
        const std::vector<float>* buffer{nullptr};
        double cursor{0.0};
        double pitch{1.0};
        float volume{0.5f};
        bool active{false};
        bool spatial{false}; // Routed through the AcousticVoiceBank filter of its slot
    };

    PlayedVoice triggerSound(SoundEffect effect, float volume, float pitchMin, float pitchMax, const Acoustics& acoustics);
    void generateProceduralSoundBank();
    void startVoice(const AudioCommand& cmd) noexcept;
    void mixAudioFrames(float* outInterleavedStereo, uint32_t frameCount) noexcept;

    static void maDataCallback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount);

    ma_context m_context{};
    ma_device m_device{};
    bool m_contextInitialized{false};
    bool m_deviceInitialized{false};

    std::array<std::vector<float>, static_cast<size_t>(SoundEffect::Count)> m_soundBuffers{};
    // One acoustic filter per mixer voice: slot i of the bank is voice i.
    std::array<ActiveVoice, acoustics::AcousticVoiceBank::MAX_VOICES> m_voices{};
    core::MpscRingBuffer<AudioCommand, 512> m_commandQueue;
    // ~100 KB of filter and FDN state, allocated once here, never in the callback.
    std::unique_ptr<acoustics::AcousticVoiceBank> m_acoustics;
    acoustics::VoiceHandleAllocator m_handles;

    std::atomic<bool> m_musicEnabled{false};
    double m_musicTime{0.0};

    // Rain loop: targets from the game, smoothed state owned by the mixer.
    std::atomic<float> m_rainGain{0.0f};
    std::atomic<float> m_rainCutoffHz{acoustics::OcclusionParams{}.maxCutoffHz};
    float m_rainLevel{0.0f};
    float m_rainCutoffNow{acoustics::OcclusionParams{}.maxCutoffHz};
    uint32_t m_rainNoise{0x9E3779B9U};
    float m_rainLow{0.0f};
    float m_rainBand{0.0f};
    float m_rainOcclusion{0.0f};
    float m_rainDrop{0.0f};
};

} // namespace betacraft::audio
