#pragma once

#include "acoustics/AcousticCommand.hpp"
#include "acoustics/AuxReverbBus.hpp"
#include "acoustics/VoiceAcousticFilter.hpp"
#include "core/MpscRingBuffer.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace betacraft::acoustics {

// Audio-thread owner of the whole acoustic layer: one VoiceAcousticFilter per mixer voice plus the
// single shared AuxReverbBus, fed by a lock-free MPSC command queue. Producers (game thread running the
// 10 Hz OcclusionScheduler / EnclosureMonitor, or any other thread) call post(); the audio callback:
//   bank.drainCommands();
//   on starting a sound in slot s:   bank.startVoice(s, cmd.voiceHandle, {gain, cmd.lpfCutoffHz}, cmd.reverbSend);
//   on a voice ending / being stolen: bank.stopVoice(s);   (startVoice on a stolen slot also rebinds)
//   bank.processBlock(out, frames, [&](span sub, size_t offset, size_t n) {   // any callback length
//       for each active voice v, frame i < n:  dry = bank.processVoiceSample(v, i, x[offset + i]);
//   });                                                                        // -> pan dry into sub
// (beginBlock / processVoiceSample / endBlock remain available for callers that already sub-block.)
// Commands carry producer voice handles, resolved here to the slot currently bound to them, so a late
// occlusion sample for a finished sound can never reach the sound that reused its slot.
// Nothing here allocates, locks or throws after construction (~100 KB; allocate it once, e.g. in a
// unique_ptr, since the FDN delay memory alone is 65 KB).
class AcousticVoiceBank {
public:
    static constexpr size_t MAX_VOICES = 32;      // == AudioEngine voice array
    // Spec value (Fiche 5 §5): 32 voices x 10 Hz use < 4 entries per frame, the rest absorbs bursts.
    static constexpr size_t QUEUE_CAPACITY = 512;

    explicit AcousticVoiceBank(float sampleRate = DEFAULT_SAMPLE_RATE);

    // Any thread. False when the queue is full (the command is dropped; the next 10 Hz sample fixes it).
    [[nodiscard]] bool post(const AcousticCommand& command) noexcept;

    // Audio thread only. Applies up to `maxCommands` queued commands in FIFO order; returns how many
    // were applied (invalid ones are counted in rejectedCommands(), unbound handles in staleCommands()).
    size_t drainCommands(size_t maxCommands = QUEUE_CAPACITY) noexcept;
    // Applies one command directly (audio thread), e.g. when the engine carries acoustic payloads in
    // its own command queue. False for an unknown type, the invalid handle or an unbound handle.
    bool apply(const AcousticCommand& command) noexcept;

    // Audio thread: the mixer just started a sound with producer `handle` in `slot`. Binds the handle
    // (moving it if it was bound elsewhere), snaps gain / cutoff / send to their initial values and
    // clears the filter memory left by the previous occupant. False for a bad slot or invalid handle.
    bool startVoice(size_t slot, uint32_t handle, const OcclusionTarget& initial,
                    float reverbSend = VoiceAcousticFilter::DEFAULT_REVERB_SEND) noexcept;
    // Audio thread: the sound in `slot` ended; later commands for its handle are dropped as stale.
    void stopVoice(size_t slot) noexcept;
    [[nodiscard]] uint32_t handleAt(size_t slot) const noexcept {
        return slot < MAX_VOICES ? m_handles[slot] : INVALID_VOICE_HANDLE;
    }
    [[nodiscard]] std::optional<size_t> slotOf(uint32_t handle) const noexcept;

    [[nodiscard]] size_t beginBlock(size_t frames) noexcept;
    // Filters one sample of `voice` (a slot index), adds its post-occlusion send into the shared bus at
    // `frame` and returns the dry sample. An out-of-range voice passes `x` through untouched.
    [[nodiscard]] float processVoiceSample(size_t voice, size_t frame, float x) noexcept;
    // Runs the shared FDN once and ADDS its wet output into the interleaved stereo buffer.
    void endBlock(std::span<float> interleavedStereoOut, size_t frames) noexcept;

    // Whole audio callback of any length: splits it into sub-blocks of at most MAX_BLOCK_FRAMES (the
    // bus scratch size) so that no frame loses its reverb send or wet signal, however many frames the
    // backend delivers. `render(subOut, frameOffset, frames)` must mix every voice for frames
    // [0, frames) of the sub-block (calling processVoiceSample(v, i, x) with i < frames, reading the
    // voice's source at frameOffset + i) and write the dry signal into subOut; the shared wet is then
    // added on top. Processes min(frames, out.size() / 2) frames.
    template <typename RenderFn>
    void processBlock(std::span<float> interleavedStereoOut, size_t frames, RenderFn&& render) noexcept {
        const size_t total = std::min(frames, interleavedStereoOut.size() / 2);
        for (size_t offset = 0; offset < total;) {
            const size_t n = beginBlock(total - offset);
            const std::span<float> sub = interleavedStereoOut.subspan(2 * offset, 2 * n);
            render(sub, offset, n);
            endBlock(sub, n);
            offset += n;
        }
    }
    // Sends that fell outside the current bus block (a caller bypassing processBlock() with a callback
    // longer than MAX_BLOCK_FRAMES); should stay 0.
    [[nodiscard]] uint64_t droppedSendFrames() const noexcept { return m_bus.droppedSendFrames(); }

    [[nodiscard]] const VoiceAcousticFilter& voice(size_t index) const noexcept { return m_voices[index]; }
    [[nodiscard]] const AuxReverbBus& bus() const noexcept { return m_bus; }
    [[nodiscard]] uint64_t rejectedCommands() const noexcept { return m_rejected; }
    [[nodiscard]] uint64_t staleCommands() const noexcept { return m_stale; }

private:
    std::array<VoiceAcousticFilter, MAX_VOICES> m_voices;
    std::array<uint32_t, MAX_VOICES> m_handles{}; // INVALID_VOICE_HANDLE = free slot
    AuxReverbBus m_bus;
    core::MpscRingBuffer<AcousticCommand, QUEUE_CAPACITY> m_queue;
    uint64_t m_rejected{0};
    uint64_t m_stale{0};
};

} // namespace betacraft::acoustics
