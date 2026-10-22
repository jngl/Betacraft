#pragma once

#include "acoustics/EnclosureDetector.hpp"
#include "acoustics/FdnReverb.hpp"
#include "acoustics/OnePoleFilter.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace betacraft::acoustics {

// The single auxiliary reverb send bus of the mixer (spec Diagram 4: "Aux Reverb Send Bus Mono ->
// Processeur FDN 8 Lignes Unique"). Every voice adds its post-occlusion signal scaled by its own send
// into one mono accumulator; the FDN runs ONCE per block on that sum and its stereo wet output is
// added onto the dry mix. DSP cost and memory are therefore independent of the voice count.
//
// Per audio block (audio thread, no allocation, no locks):
//   const size_t n = bus.beginBlock(frames);    // clears the send accumulator (n <= 512)
//   for each voice: bus.accumulate(voiceSend, sendGain);
//   bus.processAndMix(interleavedStereoOut, n); // adds wet L/R onto the dry mix
// Callers with blocks longer than MAX_BLOCK_FRAMES loop over sub-blocks (AcousticVoiceBank::processBlock
// does it for them); sends that land outside the current block are counted in droppedSendFrames().
class AuxReverbBus {
public:
    static constexpr size_t MAX_BLOCK_FRAMES = 512; // == SpatialAudioEngine::AUDIO_BUFFER_FRAMES (spec)
    // Ramps for parameter changes arriving at 10 Hz from the enclosure detector.
    static constexpr float PARAM_SMOOTHING_SECONDS = 0.05f;
    // Bus send level multiplies every voice's own send. Unity is the spec's neutral: a voice's default
    // 0.15 reaches the FDN unchanged, so the reverb is audible before any Environment update arrives.
    static constexpr float DEFAULT_SEND_LEVEL = 1.0f;
    // Enclosure may push a cave wetter than neutral; the cap bounds the FDN input (and headroom).
    static constexpr float MAX_SEND_LEVEL = 2.0f;

    explicit AuxReverbBus(float sampleRate = DEFAULT_SAMPLE_RATE);

    // Starts a block of `frames` frames; returns the number actually usable (clamped to capacity).
    [[nodiscard]] size_t beginBlock(size_t frames) noexcept;

    // Adds voiceMono[i] * sendGain into the send accumulator for the current block. Extra samples are
    // ignored; a non-finite gain drops the contribution.
    void accumulate(std::span<const float> voiceMono, float sendGain) noexcept;
    // Single-sample variant for mixers that produce voices sample by sample.
    void accumulateSample(size_t frame, float value) noexcept {
        if (frame < m_frames) {
            m_send[frame] += value;
        } else {
            ++m_droppedSendFrames; // Caller forgot to sub-block: make the lost reverb observable
        }
    }
    [[nodiscard]] uint64_t droppedSendFrames() const noexcept { return m_droppedSendFrames; }

    // Runs the shared FDN on the accumulated send and ADDS the wet signal into the interleaved
    // stereo buffer (L, R, L, R ...). Processes min(frames, current block, out.size() / 2) frames.
    void processAndMix(std::span<float> interleavedStereoOut, size_t frames) noexcept;

    // Bus-wide send multiplier in [0, MAX_SEND_LEVEL] and FDN decay/damping (from EnclosureDetector).
    // A positive decaySeconds is converted to the loop gain HERE, at the bus's own sample rate (the
    // FDN delays are fixed sample counts, so the same g gives a different RT60 at 48 kHz); a zero
    // decay means "use params.feedback as given". `snap` skips the ramp, e.g. on world load or teleport.
    void setReverbParams(const ReverbParams& params, bool snap = false) noexcept;

    void reset() noexcept;

    [[nodiscard]] const FdnReverbProcessor& reverb() const noexcept { return m_fdn; }
    // FDN delay memory + the three block scratch buffers: the bus's whole DSP working set.
    [[nodiscard]] size_t memoryBytes() const noexcept {
        return m_fdn.memoryBytes() + (m_send.size() + m_wetLeft.size() + m_wetRight.size()) * sizeof(float);
    }

private:
    FdnReverbProcessor m_fdn;
    float m_sampleRate;
    std::array<float, MAX_BLOCK_FRAMES> m_send{};
    std::array<float, MAX_BLOCK_FRAMES> m_wetLeft{};
    std::array<float, MAX_BLOCK_FRAMES> m_wetRight{};
    ParameterSmoother m_sendLevel; // Per sample: scales the summed send (1 = neutral)
    ParameterSmoother m_feedback;  // Per block: FDN parameters change slowly
    ParameterSmoother m_damping;
    size_t m_frames{0};
    uint64_t m_droppedSendFrames{0};
};

} // namespace betacraft::acoustics
