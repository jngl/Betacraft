#pragma once

#include "acoustics/AcousticOcclusion.hpp"
#include "acoustics/EnclosureDetector.hpp"
#include "acoustics/OcclusionScheduler.hpp"

#include <atomic>
#include <cstdint>

namespace betacraft::acoustics {

// Voice handles are producer-assigned ids (spec AudioCommand::voiceHandle), NOT mixer slot indices:
// the audio thread chooses the slot (first free voice, or steals one) and binds the handle to it with
// AcousticVoiceBank::startVoice(). 0 is never handed out, so a free slot can never match a command.
inline constexpr uint32_t INVALID_VOICE_HANDLE = 0;

// Lock-free source of unique voice handles for any number of producer threads. 32 bits at one
// sound per frame wrap after years; on wrap the invalid handle is skipped.
class VoiceHandleAllocator {
public:
    explicit VoiceHandleAllocator(uint32_t first = 1) noexcept : m_next(first) {}

    [[nodiscard]] uint32_t next() noexcept {
        uint32_t handle = m_next.fetch_add(1, std::memory_order_relaxed);
        if (handle == INVALID_VOICE_HANDLE) {
            handle = m_next.fetch_add(1, std::memory_order_relaxed);
        }
        return handle;
    }

private:
    std::atomic<uint32_t> m_next;
};

enum class AcousticCommandType : uint8_t {
    VoiceOcclusion = 0, // New 10 Hz occlusion target for a playing voice (smoothed)
    VoiceReverbSend,    // Per-voice aux send level (smoothed)
    Environment         // Listener surroundings changed: bus send level + FDN decay/damping
};

// Producer -> audio thread message for the acoustic layer. Trivially copyable and 32 bytes so it
// travels through core::MpscRingBuffer by value; one flat struct instead of a variant keeps the
// queue cell small and the consumer branch-light. Voice commands address `voiceHandle`; a command
// whose handle is no longer bound to any slot (the sound ended or was stolen) is dropped as stale.
//
// Starting a voice is deliberately NOT a command: the mixer calls AcousticVoiceBank::startVoice() at
// the moment it assigns the slot (from the play command's lpfCutoffHz / reverbSend fields), so the new
// sound's first block can never run with the previous occupant's filter state.
struct AcousticCommand {
    AcousticCommandType type{AcousticCommandType::VoiceOcclusion};
    bool snap{false}; // Environment only: skip the parameter ramp (world load, teleport)
    uint32_t voiceHandle{INVALID_VOICE_HANDLE};
    float gain{1.0f};
    float cutoffHz{20000.0f};
    float send{0.0f}; // Voice commands: per-voice send. Environment: bus-wide send level
    float feedback{0.0f};
    float damping{0.0f};
    float decaySeconds{0.0f}; // Environment: > 0 lets the bus derive feedback for its own sample rate

    [[nodiscard]] static constexpr AcousticCommand occlusion(uint32_t handle, const OcclusionTarget& t) noexcept {
        AcousticCommand c;
        c.type = AcousticCommandType::VoiceOcclusion;
        c.voiceHandle = handle;
        c.gain = t.gain;
        c.cutoffHz = t.cutoffHz;
        return c;
    }

    [[nodiscard]] static constexpr AcousticCommand reverbSend(uint32_t handle, float send) noexcept {
        AcousticCommand c;
        c.type = AcousticCommandType::VoiceReverbSend;
        c.voiceHandle = handle;
        c.send = send;
        return c;
    }

    [[nodiscard]] static constexpr AcousticCommand environment(const ReverbParams& p, bool snapNow = false) noexcept {
        AcousticCommand c;
        c.type = AcousticCommandType::Environment;
        c.snap = snapNow;
        c.send = p.sendLevel;
        c.feedback = p.feedback;
        c.damping = p.damping;
        c.decaySeconds = p.decaySeconds;
        return c;
    }

    // OcclusionScheduler handles are the producer-assigned voice handles (not slot indices).
    [[nodiscard]] static constexpr AcousticCommand fromUpdate(const OcclusionUpdate& u) noexcept {
        return occlusion(u.voiceHandle, OcclusionTarget{u.gain, u.cutoffHz});
    }
};

} // namespace betacraft::acoustics
