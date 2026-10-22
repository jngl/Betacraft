#include "audio/AudioEngine.hpp"
#include "core/Logger.hpp"
#include "miniaudio.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <span>

namespace betacraft::audio {

struct AudioEngine::Device {
    ma_context context{};
    ma_device device{};
    bool contextInitialized{false};
    bool deviceInitialized{false};

    static void dataCallback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount);
};

// The sound bank and the acoustic state are plain CPU data, built here so that the mixing path
// (renderFrames) works without any audio device.
AudioEngine::AudioEngine()
    : m_device(std::make_unique<Device>())
    , m_acoustics(std::make_unique<acoustics::AcousticVoiceBank>(static_cast<float>(SAMPLE_RATE))) {
    generateProceduralSoundBank();
}

AudioEngine::~AudioEngine() noexcept {
    shutdown();
}

void AudioEngine::Device::dataCallback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    (void)pInput;
    auto* engine = static_cast<AudioEngine*>(pDevice->pUserData);
    if (!engine || !pOutput) {
        return;
    }
    engine->mixAudioFrames(static_cast<float*>(pOutput), frameCount);
}

bool AudioEngine::initialize() {
    if (ma_context_init(nullptr, 0, nullptr, &m_device->context) == MA_SUCCESS) {
        m_device->contextInitialized = true;
    }

    ma_device_config devConfig = ma_device_config_init(ma_device_type_playback);
    devConfig.playback.format = ma_format_f32;
    devConfig.playback.channels = 2;
    devConfig.sampleRate = SAMPLE_RATE;
    devConfig.dataCallback = &Device::dataCallback;
    devConfig.pUserData = this;

    ma_result res = ma_device_init(m_device->contextInitialized ? &m_device->context : nullptr, &devConfig, &m_device->device);
    if (res != MA_SUCCESS) {
        // Fallback to miniaudio null backend in headless environments
        if (m_device->contextInitialized) {
            ma_context_uninit(&m_device->context);
            m_device->contextInitialized = false;
        }
        const ma_backend nullBackend = ma_backend_null;
        if (ma_context_init(&nullBackend, 1, nullptr, &m_device->context) == MA_SUCCESS) {
            m_device->contextInitialized = true;
            res = ma_device_init(&m_device->context, &devConfig, &m_device->device);
        }
    }

    if (res != MA_SUCCESS) {
        core::Logger::warn("AudioEngine", "Could not initialize audio playback device (continuing silent).");
        return true;
    }

    m_device->deviceInitialized = true;
    if (ma_device_start(&m_device->device) != MA_SUCCESS) {
        core::Logger::warn("AudioEngine", "Failed to start miniaudio playback device.");
    } else {
        core::Logger::info("AudioEngine", "Initialized miniaudio engine (", SAMPLE_RATE, " Hz stereo).");
    }

    return true;
}

bool AudioEngine::hasDevice() const noexcept {
    return m_device->deviceInitialized;
}

void AudioEngine::shutdown() noexcept {
    if (m_device->deviceInitialized) {
        ma_device_uninit(&m_device->device);
        m_device->deviceInitialized = false;
    }
    if (m_device->contextInitialized) {
        ma_context_uninit(&m_device->context);
        m_device->contextInitialized = false;
    }
}

void AudioEngine::generateProceduralSoundBank() {
    std::mt19937 rng(42173U);
    std::uniform_real_distribution<float> white(-1.0f, 1.0f);

    auto makeFilteredBurst = [&](float durationSec, float lowPassAlpha, float bodyFreq, float bodyAmt, float decayRate) {
        const size_t samples = static_cast<size_t>(durationSec * static_cast<float>(SAMPLE_RATE));
        std::vector<float> buf(samples, 0.0f);
        float lp = 0.0f;
        for (size_t i = 0; i < samples; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(SAMPLE_RATE);
            const float env = std::exp(-t * decayRate);
            const float n = white(rng);
            lp += lowPassAlpha * (n - lp);
            const float body = std::sin(6.2831853f * bodyFreq * t * std::exp(-t * 8.0f)) * bodyAmt;
            buf[i] = std::clamp((lp + body) * env, -1.0f, 1.0f);
        }
        return buf;
    };

    m_soundBuffers[static_cast<size_t>(SoundEffect::StepStone)]  = makeFilteredBurst(0.11f, 0.32f, 165.0f, 0.45f, 34.0f);
    m_soundBuffers[static_cast<size_t>(SoundEffect::StepGrass)]  = makeFilteredBurst(0.13f, 0.55f, 95.0f,  0.18f, 28.0f);
    m_soundBuffers[static_cast<size_t>(SoundEffect::StepDirt)]   = makeFilteredBurst(0.12f, 0.25f, 110.0f, 0.30f, 30.0f);
    m_soundBuffers[static_cast<size_t>(SoundEffect::StepWood)]   = makeFilteredBurst(0.12f, 0.22f, 195.0f, 0.65f, 32.0f);
    m_soundBuffers[static_cast<size_t>(SoundEffect::StepSand)]   = makeFilteredBurst(0.14f, 0.68f, 80.0f,  0.10f, 25.0f);
    m_soundBuffers[static_cast<size_t>(SoundEffect::StepCloth)]  = makeFilteredBurst(0.11f, 0.16f, 90.0f,  0.25f, 35.0f);

    // Multi-crunch block break sounds
    auto makeBreakSound = [&](float baseLp, float bodyFreq) {
        const size_t samples = static_cast<size_t>(0.28f * static_cast<float>(SAMPLE_RATE));
        std::vector<float> buf(samples, 0.0f);
        float lp = 0.0f;
        for (size_t i = 0; i < samples; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(SAMPLE_RATE);
            const float subPulse = 0.6f + 0.4f * std::sin(t * 95.0f);
            const float env = std::exp(-t * 14.0f) * subPulse;
            lp += baseLp * (white(rng) - lp);
            const float thump = std::sin(6.2831853f * bodyFreq * t) * 0.35f;
            buf[i] = std::clamp((lp + thump) * env, -1.0f, 1.0f);
        }
        return buf;
    };

    m_soundBuffers[static_cast<size_t>(SoundEffect::BreakStone)] = makeBreakSound(0.36f, 145.0f);
    m_soundBuffers[static_cast<size_t>(SoundEffect::BreakGrass)] = makeBreakSound(0.52f, 105.0f);
    m_soundBuffers[static_cast<size_t>(SoundEffect::BreakWood)]  = makeBreakSound(0.24f, 180.0f);

    // Crisp Glass Shatter
    {
        const size_t samples = static_cast<size_t>(0.34f * static_cast<float>(SAMPLE_RATE));
        std::vector<float> buf(samples, 0.0f);
        for (size_t i = 0; i < samples; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(SAMPLE_RATE);
            const float env = std::exp(-t * 12.0f);
            const float chime =
                std::sin(6.2831853f * 1850.0f * t) * 0.3f +
                std::sin(6.2831853f * 2740.0f * t) * 0.25f +
                std::sin(6.2831853f * 3620.0f * t) * 0.2f +
                white(rng) * 0.35f;
            buf[i] = std::clamp(chime * env, -1.0f, 1.0f);
        }
        m_soundBuffers[static_cast<size_t>(SoundEffect::BreakGlass)] = std::move(buf);
    }

    // Water Splash
    m_soundBuffers[static_cast<size_t>(SoundEffect::WaterSplash)] = makeFilteredBurst(0.38f, 0.18f, 240.0f, 0.25f, 8.5f);

    // Beta 1.7 Piston mechanical extend clunk
    {
        const size_t samples = static_cast<size_t>(0.24f * static_cast<float>(SAMPLE_RATE));
        std::vector<float> buf(samples, 0.0f);
        float lp = 0.0f;
        for (size_t i = 0; i < samples; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(SAMPLE_RATE);
            const float env = std::exp(-t * 16.0f);
            lp += 0.25f * (white(rng) - lp);
            const float servo = std::sin(6.2831853f * (120.0f + t * 280.0f) * t) * 0.55f;
            buf[i] = std::clamp((lp + servo) * env, -1.0f, 1.0f);
        }
        m_soundBuffers[static_cast<size_t>(SoundEffect::PistonExtend)] = std::move(buf);
    }

    // Classic Beta UI Click
    m_soundBuffers[static_cast<size_t>(SoundEffect::UiClick)] = makeFilteredBurst(0.045f, 0.45f, 620.0f, 0.65f, 65.0f);

    // Lava/water mix (Beta random.fizz): bright hiss, no tonal body, slow decay
    m_soundBuffers[static_cast<size_t>(SoundEffect::LavaFizz)] = makeFilteredBurst(0.5f, 0.85f, 0.0f, 0.0f, 6.0f);

    // Hurt (Beta damage.hurtflesh): a short voiced grunt, a falling buzzy tone over soft noise
    {
        const size_t samples = static_cast<size_t>(0.26f * static_cast<float>(SAMPLE_RATE));
        std::vector<float> buf(samples, 0.0f);
        float lp = 0.0f;
        float phase = 0.0f;
        for (size_t i = 0; i < samples; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(SAMPLE_RATE);
            const float env = std::min(1.0f, t * 120.0f) * std::exp(-t * 11.0f);
            phase += 6.2831853f * (260.0f - t * 380.0f) / static_cast<float>(SAMPLE_RATE);
            const float voice = std::sin(phase) * 0.55f + std::sin(phase * 2.0f) * 0.22f + std::sin(phase * 3.0f) * 0.1f;
            lp += 0.2f * (white(rng) - lp);
            buf[i] = std::clamp((voice + lp * 0.35f) * env, -1.0f, 1.0f);
        }
        m_soundBuffers[static_cast<size_t>(SoundEffect::Hurt)] = std::move(buf);
    }

    // Falls (Beta damage.fallsmall / fallbig): dull thuds, the big one longer and lower
    m_soundBuffers[static_cast<size_t>(SoundEffect::FallSmall)] = makeFilteredBurst(0.16f, 0.12f, 95.0f, 0.7f, 26.0f);
    m_soundBuffers[static_cast<size_t>(SoundEffect::FallBig)]   = makeFilteredBurst(0.38f, 0.10f, 62.0f, 0.9f, 11.0f);

    // Pop (Beta random.pop): a quick upward sine chirp
    {
        const size_t samples = static_cast<size_t>(0.07f * static_cast<float>(SAMPLE_RATE));
        std::vector<float> buf(samples, 0.0f);
        float phase = 0.0f;
        for (size_t i = 0; i < samples; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(SAMPLE_RATE);
            const float env = std::min(1.0f, t * 900.0f) * std::exp(-t * 55.0f);
            phase += 6.2831853f * (520.0f + t * 9000.0f) / static_cast<float>(SAMPLE_RATE);
            buf[i] = std::clamp(std::sin(phase) * 0.8f * env, -1.0f, 1.0f);
        }
        m_soundBuffers[static_cast<size_t>(SoundEffect::Pop)] = std::move(buf);
    }

    // Eat: three crunchy bites of band-limited noise
    {
        const size_t samples = static_cast<size_t>(0.36f * static_cast<float>(SAMPLE_RATE));
        std::vector<float> buf(samples, 0.0f);
        float lp = 0.0f;
        float hp = 0.0f;
        for (size_t i = 0; i < samples; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(SAMPLE_RATE);
            const float local = std::fmod(t, 0.12f);
            const float env = std::exp(-local * 38.0f) * std::exp(-t * 2.5f);
            const float n = white(rng);
            lp += 0.45f * (n - lp);
            hp = lp - hp * 0.6f;
            buf[i] = std::clamp(hp * 0.7f * env, -1.0f, 1.0f);
        }
        m_soundBuffers[static_cast<size_t>(SoundEffect::Eat)] = std::move(buf);
    }

    // Voiced mob calls: a harmonic tone whose pitch glides from f0 to f1, an attack/decay
    // envelope, optional vibrato (bleat) and a little breath noise.
    auto makeVoice = [&](float durationSec, float f0, float f1, float vibratoHz, float vibratoDepth,
                         float buzz, float noiseAmt, float decayRate) {
        const size_t samples = static_cast<size_t>(durationSec * static_cast<float>(SAMPLE_RATE));
        std::vector<float> buf(samples, 0.0f);
        float phase = 0.0f;
        float lp = 0.0f;
        for (size_t i = 0; i < samples; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(SAMPLE_RATE);
            const float u = t / durationSec;
            const float env = std::min(1.0f, t * 40.0f) * std::exp(-t * decayRate) * (1.0f - u * u);
            const float freq = (f0 + (f1 - f0) * u) * (1.0f + vibratoDepth * std::sin(6.2831853f * vibratoHz * t));
            phase += 6.2831853f * freq / static_cast<float>(SAMPLE_RATE);
            const float voice = std::sin(phase) * 0.5f + std::sin(phase * 2.0f) * 0.25f * buzz +
                                std::sin(phase * 3.0f) * 0.15f * buzz;
            lp += 0.25f * (white(rng) - lp);
            buf[i] = std::clamp((voice + lp * noiseAmt) * env, -1.0f, 1.0f);
        }
        return buf;
    };
    // Zombie groan: long, low and buzzy, sagging in pitch
    m_soundBuffers[static_cast<size_t>(SoundEffect::ZombieGroan)] = makeVoice(0.85f, 110.0f, 80.0f, 5.0f, 0.04f, 1.0f, 0.35f, 1.5f);
    // Pig oink: two short nasal grunts
    {
        const std::vector<float> first = makeVoice(0.12f, 230.0f, 170.0f, 0.0f, 0.0f, 1.0f, 0.45f, 10.0f);
        const std::vector<float> second = makeVoice(0.14f, 210.0f, 150.0f, 0.0f, 0.0f, 1.0f, 0.45f, 9.0f);
        const size_t gap = static_cast<size_t>(0.05f * static_cast<float>(SAMPLE_RATE));
        std::vector<float> buf(first.size() + gap + second.size(), 0.0f);
        std::copy(first.begin(), first.end(), buf.begin());
        std::copy(second.begin(), second.end(), buf.begin() + static_cast<std::ptrdiff_t>(first.size() + gap));
        m_soundBuffers[static_cast<size_t>(SoundEffect::PigOink)] = std::move(buf);
    }
    // Sheep baa: a bleating vibrato
    m_soundBuffers[static_cast<size_t>(SoundEffect::SheepBaa)] = makeVoice(0.6f, 380.0f, 330.0f, 22.0f, 0.06f, 0.7f, 0.15f, 2.0f);
    // Mob hurt: a short falling yelp
    m_soundBuffers[static_cast<size_t>(SoundEffect::MobHurt)] = makeVoice(0.2f, 300.0f, 160.0f, 0.0f, 0.0f, 0.8f, 0.4f, 9.0f);
    // Mob death: a longer falling moan
    m_soundBuffers[static_cast<size_t>(SoundEffect::MobDeath)] = makeVoice(0.55f, 240.0f, 70.0f, 7.0f, 0.03f, 0.9f, 0.3f, 3.0f);

    // Doors (Beta random.door_open / door_close): a wooden creak gliding up as it swings
    // open, then a knock; closing glides down and ends on a firmer knock.
    auto makeDoor = [&](float f0, float f1, float knockGain) {
        const size_t samples = static_cast<size_t>(0.32f * static_cast<float>(SAMPLE_RATE));
        std::vector<float> buf(samples, 0.0f);
        float phase = 0.0f;
        float lp = 0.0f;
        for (size_t i = 0; i < samples; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(SAMPLE_RATE);
            const float u = t / 0.32f;
            const float creakEnv = std::min(1.0f, t * 60.0f) * std::exp(-t * 7.0f) * (1.0f - u);
            phase += 6.2831853f * (f0 + (f1 - f0) * u) / static_cast<float>(SAMPLE_RATE);
            // A rough sawtooth-like creak, gated at ~38 Hz like wood fibres slipping.
            const float creak = (std::fmod(phase, 6.2831853f) / 3.14159265f - 1.0f) *
                                (0.6f + 0.4f * std::sin(6.2831853f * 38.0f * t));
            lp += 0.3f * (white(rng) - lp);
            const float kt = t - 0.22f;
            const float knock = kt >= 0.0f ? std::exp(-kt * 45.0f) * (std::sin(6.2831853f * 150.0f * kt) + lp) : 0.0f;
            buf[i] = std::clamp(creak * creakEnv * 0.35f + knock * knockGain, -1.0f, 1.0f);
        }
        return buf;
    };
    m_soundBuffers[static_cast<size_t>(SoundEffect::DoorOpen)] = makeDoor(180.0f, 320.0f, 0.35f);
    m_soundBuffers[static_cast<size_t>(SoundEffect::DoorClose)] = makeDoor(300.0f, 160.0f, 0.75f);

    // Click (Beta random.click): a short, dry mechanical tick with a little metallic ring.
    m_soundBuffers[static_cast<size_t>(SoundEffect::Click)] = makeFilteredBurst(0.06f, 0.6f, 1800.0f, 0.5f, 80.0f);

    // Explosion (Beta random.explode): a hard low thump under a long rumbling noise tail.
    {
        constexpr float kSeconds = 1.6f;
        const size_t samples = static_cast<size_t>(kSeconds * static_cast<float>(SAMPLE_RATE));
        std::vector<float> buf(samples, 0.0f);
        float lp = 0.0f;
        float lp2 = 0.0f;
        for (size_t i = 0; i < samples; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(SAMPLE_RATE);
            lp += 0.08f * (white(rng) - lp);
            lp2 += 0.02f * (lp - lp2);
            const float rumble = (lp * 1.4f + lp2 * 6.0f) * std::exp(-t * 2.6f);
            const float thump = std::sin(6.2831853f * 55.0f * t * std::exp(-t * 3.0f)) * std::exp(-t * 9.0f);
            const float attack = std::min(1.0f, t * 400.0f);
            buf[i] = std::clamp((rumble + thump) * attack, -1.0f, 1.0f);
        }
        m_soundBuffers[static_cast<size_t>(SoundEffect::Explosion)] = std::move(buf);
    }

    // Thunder (Beta ambient.weather.thunder): a bright crack, then a long low rumble that
    // swells and dies away over a few seconds.
    {
        constexpr float kSeconds = 4.5f;
        const size_t samples = static_cast<size_t>(kSeconds * static_cast<float>(SAMPLE_RATE));
        std::vector<float> buf(samples, 0.0f);
        float lp = 0.0f;
        float lp2 = 0.0f;
        float crack = 0.0f;
        for (size_t i = 0; i < samples; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(SAMPLE_RATE);
            const float n = white(rng);
            crack += 0.7f * (n - crack);
            lp += 0.03f * (n - lp);
            lp2 += 0.01f * (lp - lp2);
            const float crackEnv = std::exp(-t * 18.0f);
            const float swell = 0.65f + 0.35f * std::sin(t * 5.3f) * std::sin(t * 2.1f + 0.7f);
            const float rumbleEnv = std::min(1.0f, t * 6.0f) * std::exp(-t * 0.9f) * swell;
            const float attack = std::min(1.0f, t * 800.0f);
            buf[i] = std::clamp((crack * crackEnv * 0.8f + (lp * 3.0f + lp2 * 9.0f) * rumbleEnv) * attack, -1.0f, 1.0f);
        }
        m_soundBuffers[static_cast<size_t>(SoundEffect::Thunder)] = std::move(buf);
    }
}

PlayedVoice AudioEngine::playSound(SoundEffect effect, float volume, float pitch, const Acoustics& acoustics) {
    AudioCommand cmd{.type = AudioCommandType::PlaySound, .effect = effect, .volume = volume, .pitch = pitch};
    PlayedVoice voice{};
    if (acoustics) {
        voice.handle = m_handles.next();
        cmd.voiceHandle = voice.handle;
        cmd.occlusionGain = acoustics->occlusionGain;
        cmd.lpfCutoffHz = acoustics->lpfCutoffHz;
        cmd.reverbSend = acoustics->reverbSend;
    }
    // Without a device nobody drains the queue: it fills up and later sounds are dropped here.
    if (!m_commandQueue.push(cmd)) {
        return {};
    }
    // The buffers are immutable after construction, so their length is safe to read here.
    const size_t effIdx = static_cast<size_t>(effect);
    if (effIdx < m_soundBuffers.size() && pitch > 0.0f) {
        voice.durationSeconds = static_cast<float>(static_cast<double>(m_soundBuffers[effIdx].size()) /
                                                   (static_cast<double>(pitch) * SAMPLE_RATE));
    }
    return voice;
}

PlayedVoice AudioEngine::triggerSound(SoundEffect effect, float volume, float pitchMin, float pitchMax, const Acoustics& acoustics) {
    thread_local std::mt19937 tl_rng{std::random_device{}()};
    std::uniform_real_distribution<float> pitchDist(pitchMin, pitchMax);
    return playSound(effect, volume, pitchDist(tl_rng), acoustics);
}

bool AudioEngine::postAcoustic(const acoustics::AcousticCommand& command) noexcept {
    AudioCommand cmd{};
    cmd.type = AudioCommandType::Acoustic;
    cmd.acoustic = command;
    return m_commandQueue.push(cmd);
}

bool AudioEngine::renderFrames(float* outInterleavedStereo, uint32_t frameCount) noexcept {
    if (m_device->deviceInitialized || outInterleavedStereo == nullptr) {
        return false;
    }
    mixAudioFrames(outInterleavedStereo, frameCount);
    return true;
}

void AudioEngine::stopAll() {
    (void)m_commandQueue.push(AudioCommand{
        .type = AudioCommandType::StopAll,
        .effect = SoundEffect::StepStone,
        .volume = 0.0f,
        .pitch = 1.0f
    });
}

PlayedVoice AudioEngine::playBlockStep(voxel::SoundCategory category, const Acoustics& acoustics) {
    switch (category) {
        case voxel::SoundCategory::Grass: return triggerSound(SoundEffect::StepGrass, 0.22f, 0.90f, 1.12f, acoustics);
        case voxel::SoundCategory::Dirt:  return triggerSound(SoundEffect::StepDirt,  0.22f, 0.90f, 1.12f, acoustics);
        case voxel::SoundCategory::Wood:  return triggerSound(SoundEffect::StepWood,  0.25f, 0.90f, 1.10f, acoustics);
        case voxel::SoundCategory::Sand:  return triggerSound(SoundEffect::StepSand,  0.22f, 0.92f, 1.12f, acoustics);
        case voxel::SoundCategory::Cloth: return triggerSound(SoundEffect::StepCloth, 0.20f, 0.92f, 1.12f, acoustics);
        default:                          return triggerSound(SoundEffect::StepStone, 0.24f, 0.90f, 1.10f, acoustics);
    }
}

PlayedVoice AudioEngine::playBlockDigHit(voxel::SoundCategory category, const Acoustics& acoustics) {
    switch (category) {
        case voxel::SoundCategory::Grass: return triggerSound(SoundEffect::StepGrass, 0.18f, 0.75f, 0.92f, acoustics);
        case voxel::SoundCategory::Wood:  return triggerSound(SoundEffect::StepWood,  0.20f, 0.75f, 0.92f, acoustics);
        default:                          return triggerSound(SoundEffect::StepStone, 0.20f, 0.75f, 0.92f, acoustics);
    }
}

PlayedVoice AudioEngine::playBlockBreak(voxel::SoundCategory category, const Acoustics& acoustics) {
    switch (category) {
        case voxel::SoundCategory::Grass:
        case voxel::SoundCategory::Dirt:
        case voxel::SoundCategory::Sand:
        case voxel::SoundCategory::Cloth:
            return triggerSound(SoundEffect::BreakGrass, 0.42f, 0.85f, 1.05f, acoustics);
        case voxel::SoundCategory::Wood:
            return triggerSound(SoundEffect::BreakWood, 0.45f, 0.85f, 1.05f, acoustics);
        case voxel::SoundCategory::Glass:
            return triggerSound(SoundEffect::BreakGlass, 0.45f, 0.92f, 1.10f, acoustics);
        default:
            return triggerSound(SoundEffect::BreakStone, 0.45f, 0.84f, 1.04f, acoustics);
    }
}

PlayedVoice AudioEngine::playBlockPlace(voxel::SoundCategory category, const Acoustics& acoustics) {
    switch (category) {
        case voxel::SoundCategory::Wood:
            return triggerSound(SoundEffect::BreakWood, 0.38f, 1.05f, 1.22f, acoustics);
        case voxel::SoundCategory::Grass:
        case voxel::SoundCategory::Dirt:
        case voxel::SoundCategory::Sand:
            return triggerSound(SoundEffect::BreakGrass, 0.38f, 1.05f, 1.22f, acoustics);
        default:
            return triggerSound(SoundEffect::BreakStone, 0.38f, 1.05f, 1.22f, acoustics);
    }
}

PlayedVoice AudioEngine::playWaterSplash(const Acoustics& acoustics) {
    return triggerSound(SoundEffect::WaterSplash, 0.45f, 0.90f, 1.10f, acoustics);
}

PlayedVoice AudioEngine::playPistonSound(const Acoustics& acoustics) {
    return triggerSound(SoundEffect::PistonExtend, 0.48f, 0.94f, 1.06f, acoustics);
}

void AudioEngine::playUiClick() {
    (void)triggerSound(SoundEffect::UiClick, 0.35f, 0.98f, 1.04f, std::nullopt);
}

PlayedVoice AudioEngine::playLavaFizz(float volumeScale, const Acoustics& acoustics) {
    // Beta: volume 0.5, pitch 2.6 + (rand - rand) * 0.8, relative to a lower-pitched hiss here.
    return triggerSound(SoundEffect::LavaFizz, 0.5f * std::clamp(volumeScale, 0.0f, 1.0f), 0.85f, 1.25f, acoustics);
}

PlayedVoice AudioEngine::playThunder(float volume) {
    // Beta: pitch 0.8 + rand * 0.2.
    return triggerSound(SoundEffect::Thunder, std::clamp(volume, 0.0f, 1.0f), 0.8f, 1.0f, std::nullopt);
}

void AudioEngine::setRainLoop(float gain, float lpfCutoffHz) noexcept {
    const acoustics::OcclusionParams limits{};
    m_rainGain.store(std::isfinite(gain) ? std::clamp(gain, 0.0f, 1.0f) : 0.0f, std::memory_order_relaxed);
    m_rainCutoffHz.store(std::isfinite(lpfCutoffHz) ? std::clamp(lpfCutoffHz, limits.minCutoffHz, limits.maxCutoffHz)
                                                    : limits.maxCutoffHz,
                         std::memory_order_relaxed);
}

void AudioEngine::startVoice(const AudioCommand& cmd) noexcept {
    const size_t effIdx = static_cast<size_t>(cmd.effect);
    if (effIdx >= m_soundBuffers.size() || m_soundBuffers[effIdx].empty()) {
        return;
    }
    size_t slot = 0; // Steal voice 0 if all are active
    for (size_t i = 0; i < m_voices.size(); ++i) {
        if (!m_voices[i].active) {
            slot = i;
            break;
        }
    }
    const bool spatial = cmd.voiceHandle != acoustics::INVALID_VOICE_HANDLE;
    m_voices[slot] = ActiveVoice{
        .buffer = &m_soundBuffers[effIdx],
        .cursor = 0.0,
        .pitch = static_cast<double>(cmd.pitch),
        .volume = cmd.volume,
        .active = true,
        .spatial = spatial
    };
    // Bound in the same place the PCM starts, so the first block never runs with the previous
    // occupant's filter state or receives its late occlusion updates.
    if (spatial) {
        (void)m_acoustics->startVoice(slot, cmd.voiceHandle, acoustics::OcclusionTarget{cmd.occlusionGain, cmd.lpfCutoffHz},
                                      cmd.reverbSend);
    } else {
        m_acoustics->stopVoice(slot);
    }
}

void AudioEngine::mixAudioFrames(float* outInterleavedStereo, uint32_t frameCount) noexcept {
    std::memset(outInterleavedStereo, 0, frameCount * 2 * sizeof(float));

    // Drain command queue in real-time callback without locks or allocations
    AudioCommand cmd{};
    while (m_commandQueue.pop(cmd)) {
        switch (cmd.type) {
            case AudioCommandType::PlaySound:
                startVoice(cmd);
                break;
            case AudioCommandType::StopAll:
                for (size_t slot = 0; slot < m_voices.size(); ++slot) {
                    m_voices[slot].active = false;
                    m_acoustics->stopVoice(slot);
                }
                break;
            case AudioCommandType::Acoustic:
                (void)m_acoustics->apply(cmd.acoustic);
                break;
        }
    }

    const bool musicOn = m_musicEnabled.load(std::memory_order_relaxed);
    constexpr double invRate = 1.0 / static_cast<double>(SAMPLE_RATE);
    const float rainTarget = m_rainGain.load(std::memory_order_relaxed);
    const float rainCutoffTarget = m_rainCutoffHz.load(std::memory_order_relaxed);
    // ~0.25 s glides for the gain and the occlusion cutoff.
    constexpr float kRainGlide = 1.0f / (0.25f * static_cast<float>(SAMPLE_RATE));
    // The occlusion low-pass coefficient follows the cutoff once per callback.
    const float rainAlpha = 1.0f - std::exp(-6.2831853f * m_rainCutoffNow / static_cast<float>(SAMPLE_RATE));
    const std::span<float> out(outInterleavedStereo, static_cast<size_t>(frameCount) * 2);

    // The bank splits the callback into sub-blocks its aux bus can hold; each one gets the dry mix
    // written here and the shared FDN's wet output added on top.
    m_acoustics->processBlock(out, frameCount, [&](std::span<float> sub, size_t, size_t frames) noexcept {
        for (size_t f = 0; f < frames; ++f) {
            float mono = 0.0f;
            for (size_t slot = 0; slot < m_voices.size(); ++slot) {
                ActiveVoice& voice = m_voices[slot];
                if (!voice.active || !voice.buffer) continue;
                const size_t idx = static_cast<size_t>(voice.cursor);
                if (idx >= voice.buffer->size()) {
                    voice.active = false;
                    m_acoustics->stopVoice(slot);
                    continue;
                }
                // Volume before the filter, so the reverb send follows how loud the sound is.
                const float sample = (*voice.buffer)[idx] * voice.volume;
                mono += voice.spatial ? m_acoustics->processVoiceSample(slot, f, sample) : sample;
                voice.cursor += voice.pitch;
            }

            // S8.12 rain loop: band-limited noise (the hiss of the downpour) plus sparse drop
            // ticks, low-passed by the occlusion of the rain around the listener.
            m_rainLevel += (rainTarget - m_rainLevel) * kRainGlide * 4.0f;
            m_rainCutoffNow += (rainCutoffTarget - m_rainCutoffNow) * kRainGlide * 4.0f;
            if (m_rainLevel > 1.0e-4f) {
                m_rainNoise = m_rainNoise * 1664525U + 1013904223U;
                const float n = static_cast<float>(m_rainNoise >> 8U) * (2.0f / 16777216.0f) - 1.0f;
                m_rainLow += 0.35f * (n - m_rainLow);
                m_rainBand += 0.05f * (m_rainLow - m_rainBand);
                float rain = (m_rainLow - m_rainBand) * 0.9f;
                if ((m_rainNoise >> 24U) == 0U) {
                    m_rainDrop = 0.6f * n; // a drop hitting something nearby
                }
                m_rainDrop *= 0.995f;
                rain += m_rainDrop * 0.3f;
                m_rainOcclusion += rainAlpha * (rain - m_rainOcclusion);
                mono += m_rainOcclusion * m_rainLevel * 0.35f;
            }

            // Optional C418-inspired peaceful ambient synth pad (toggle with M)
            if (musicOn) {
                const double t = m_musicTime;
                m_musicTime += invRate;
                // Slow 4-chord progression (Fmaj7 -> Cmaj7 -> Dm7 -> Am7)
                static constexpr double chords[4][3] = {
                    {174.61, 220.00, 261.63},
                    {130.81, 196.00, 246.94},
                    {146.83, 220.00, 261.63},
                    {110.00, 164.81, 220.00}
                };
                const int chordIdx = static_cast<int>(t / 4.0) % 4;
                const double localT = std::fmod(t, 4.0);
                const float env = static_cast<float>(std::sin(localT / 4.0 * 3.14159265));
                float musicSample = 0.0f;
                for (int n = 0; n < 3; ++n) {
                    musicSample += static_cast<float>(std::sin(6.2831853 * chords[chordIdx][n] * t)) * 0.025f;
                }
                mono += musicSample * env;
            }

            sub[f * 2 + 0] = mono;
            sub[f * 2 + 1] = mono;
        }
    });

    for (float& sample : out) {
        sample = std::clamp(sample, -1.0f, 1.0f);
    }
}

} // namespace betacraft::audio
