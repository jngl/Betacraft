#pragma once

#include "acoustics/AcousticOcclusion.hpp"
#include "acoustics/OnePoleFilter.hpp"

namespace betacraft::acoustics {

struct VoiceSample {
    float dry{0.0f};  // To the voice's stereo panner
    float send{0.0f}; // To AuxReverbBus::accumulate / accumulateSample
};

// Per-voice acoustic processing for the audio callback: occlusion low-pass + occlusion gain + aux send
// level. Targets arrive at 10 Hz (OcclusionScheduler -> MPSC -> audio thread); every parameter is
// smoothed per sample (gain and send directly, the LPF in the coefficient domain to avoid an exp()
// per sample) so updates never click. One instance per voice (~40 bytes), no allocation.
class VoiceAcousticFilter {
public:
    static constexpr float DEFAULT_SMOOTHING_SECONDS = 0.02f; // 5 tau = one 10 Hz period
    static constexpr float DEFAULT_REVERB_SEND = 0.15f;       // Spec AudioCommand::reverbSend

    explicit VoiceAcousticFilter(float sampleRate = DEFAULT_SAMPLE_RATE,
                                 float smoothingSeconds = DEFAULT_SMOOTHING_SECONDS) noexcept;

    // Non-finite fields are ignored; gain is clamped to [0, 1].
    void setOcclusionTarget(const OcclusionTarget& target) noexcept;
    void setReverbSend(float send) noexcept;
    // Jump straight to the targets: used when a voice starts so it does not fade from "unoccluded".
    void snapToTargets() noexcept;

    [[nodiscard]] VoiceSample process(float x) noexcept {
        m_lpf.setCoefficient(m_coeff.next());
        const float dry = m_lpf.process(x) * m_gain.next();
        return VoiceSample{dry, dry * m_send.next()}; // Post-occlusion send
    }

    void reset() noexcept { m_lpf.reset(); }

    [[nodiscard]] float currentGain() const noexcept { return m_gain.current(); }
    [[nodiscard]] float currentCoefficient() const noexcept { return m_coeff.current(); }

private:
    float m_sampleRate;
    OnePoleLowPass m_lpf;
    ParameterSmoother m_gain;
    ParameterSmoother m_coeff;
    ParameterSmoother m_send;
};

} // namespace betacraft::acoustics
