#include "acoustics/AcousticVoiceBank.hpp"

namespace betacraft::acoustics {

namespace {

[[nodiscard]] std::array<VoiceAcousticFilter, AcousticVoiceBank::MAX_VOICES> makeVoices(float sampleRate) noexcept {
    std::array<VoiceAcousticFilter, AcousticVoiceBank::MAX_VOICES> voices;
    voices.fill(VoiceAcousticFilter(sampleRate));
    return voices;
}

} // namespace

AcousticVoiceBank::AcousticVoiceBank(float sampleRate) : m_voices(makeVoices(sampleRate)), m_bus(sampleRate) {}

bool AcousticVoiceBank::post(const AcousticCommand& command) noexcept { return m_queue.push(command); }

size_t AcousticVoiceBank::drainCommands(size_t maxCommands) noexcept {
    size_t applied = 0;
    AcousticCommand command;
    for (size_t i = 0; i < maxCommands && m_queue.pop(command); ++i) {
        applied += apply(command) ? 1u : 0u;
    }
    return applied;
}

std::optional<size_t> AcousticVoiceBank::slotOf(uint32_t handle) const noexcept {
    if (handle == INVALID_VOICE_HANDLE) {
        return std::nullopt;
    }
    // 32 compares: cheaper than any map and needs no allocation.
    for (size_t slot = 0; slot < MAX_VOICES; ++slot) {
        if (m_handles[slot] == handle) {
            return slot;
        }
    }
    return std::nullopt;
}

bool AcousticVoiceBank::startVoice(size_t slot, uint32_t handle, const OcclusionTarget& initial,
                                   float reverbSend) noexcept {
    if (slot >= MAX_VOICES || handle == INVALID_VOICE_HANDLE) {
        return false;
    }
    if (const std::optional<size_t> previous = slotOf(handle)) {
        m_handles[*previous] = INVALID_VOICE_HANDLE;
    }
    m_handles[slot] = handle;
    VoiceAcousticFilter& voice = m_voices[slot];
    voice.setOcclusionTarget(initial);
    voice.setReverbSend(reverbSend);
    voice.snapToTargets();
    voice.reset();
    return true;
}

void AcousticVoiceBank::stopVoice(size_t slot) noexcept {
    if (slot < MAX_VOICES) {
        m_handles[slot] = INVALID_VOICE_HANDLE;
    }
}

bool AcousticVoiceBank::apply(const AcousticCommand& command) noexcept {
    switch (command.type) {
        case AcousticCommandType::Environment:
            m_bus.setReverbParams(ReverbParams{command.send, command.feedback, command.damping, command.decaySeconds},
                                  command.snap);
            return true;
        case AcousticCommandType::VoiceOcclusion:
        case AcousticCommandType::VoiceReverbSend:
            break;
        default:
            ++m_rejected; // Corrupted / future command type
            return false;
    }
    if (command.voiceHandle == INVALID_VOICE_HANDLE) {
        ++m_rejected;
        return false;
    }
    const std::optional<size_t> slot = slotOf(command.voiceHandle);
    if (!slot) {
        ++m_stale; // The sound ended or its slot was stolen: never touch the new occupant
        return false;
    }
    VoiceAcousticFilter& voice = m_voices[*slot];
    if (command.type == AcousticCommandType::VoiceOcclusion) {
        voice.setOcclusionTarget(OcclusionTarget{command.gain, command.cutoffHz});
    } else {
        voice.setReverbSend(command.send);
    }
    return true;
}

size_t AcousticVoiceBank::beginBlock(size_t frames) noexcept { return m_bus.beginBlock(frames); }

float AcousticVoiceBank::processVoiceSample(size_t voice, size_t frame, float x) noexcept {
    if (voice >= MAX_VOICES) {
        return x;
    }
    const VoiceSample s = m_voices[voice].process(x);
    m_bus.accumulateSample(frame, s.send);
    return s.dry;
}

void AcousticVoiceBank::endBlock(std::span<float> interleavedStereoOut, size_t frames) noexcept {
    m_bus.processAndMix(interleavedStereoOut, frames);
}

} // namespace betacraft::acoustics
