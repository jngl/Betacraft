#pragma once

#include <algorithm>
#include <cstdint>

namespace betacraft::fluids {

enum class FluidType : uint8_t {
    None = 0,
    Water,
    Lava
};

// Per-voxel fluid state, stored by the world as a 4-bit nibble (roadmap P2.3):
//   bit 3     : falling (vertical flow) flag
//   bits 0..2 : level / thickness, 7 = source (full), 6..0 = progressively thinner flow.
// "Dry" is expressed by FluidType::None (the block id carries the fluid type), which is
// what lets water reach the full Beta distance of 7 flowing cells from a source: the
// thinnest flowing water (Beta decay 7) is level 0, not an empty cell.
//
// The simulator reasons in Beta's native "decay" units (0 = source, 1..7 = flowing,
// >= 8 = falling, -1 = no fluid) so the update rules map 1:1 onto BlockFlowing.java;
// level = 7 - decay for non-falling fluid.
struct FluidState {
    static constexpr uint8_t kSourceLevel = 7;
    static constexpr uint8_t kLevelMask = 0x7U;
    static constexpr uint8_t kFallingBit = 0x8U;
    static constexpr int kFallingDecay = 8;

    FluidType type{FluidType::None};
    uint8_t level{0};     // 0..7, 7 = source
    bool fallingFlag{false};

    [[nodiscard]] static constexpr FluidState source(FluidType t) noexcept {
        return {t, kSourceLevel, false};
    }

    // Falling fluid always carries a full level: when it lands it spreads as if it were a
    // fresh source (Beta: "if (l >= 8) k1 = 1"), so its inherited thickness is irrelevant.
    [[nodiscard]] static constexpr FluidState falling(FluidType t) noexcept {
        return {t, kSourceLevel, true};
    }

    [[nodiscard]] static constexpr FluidState flowing(FluidType t, uint8_t lvl) noexcept {
        return {t, std::min<uint8_t>(lvl, kSourceLevel), false};
    }

    [[nodiscard]] static constexpr FluidState fromNibble(FluidType t, uint8_t nibble) noexcept {
        return {t, static_cast<uint8_t>(nibble & kLevelMask), (nibble & kFallingBit) != 0U};
    }

    // decay < 0 -> dry, decay >= 8 -> falling, otherwise level = 7 - decay.
    [[nodiscard]] static constexpr FluidState fromBetaDecay(FluidType t, int decay) noexcept {
        if (t == FluidType::None || decay < 0) {
            return {};
        }
        if (decay >= kFallingDecay) {
            return falling(t);
        }
        return {t, static_cast<uint8_t>(kSourceLevel - decay), false};
    }

    [[nodiscard]] constexpr uint8_t toNibble() const noexcept {
        return static_cast<uint8_t>((level & kLevelMask) | (fallingFlag ? kFallingBit : 0U));
    }

    [[nodiscard]] constexpr int betaDecay() const noexcept {
        if (isEmpty()) {
            return -1;
        }
        if (fallingFlag) {
            return kFallingDecay;
        }
        return kSourceLevel - static_cast<int>(level & kLevelMask);
    }

    [[nodiscard]] constexpr bool isEmpty() const noexcept { return type == FluidType::None; }
    [[nodiscard]] constexpr bool isFalling() const noexcept { return !isEmpty() && fallingFlag; }
    [[nodiscard]] constexpr bool isSource() const noexcept {
        return !isEmpty() && !fallingFlag && level == kSourceLevel;
    }

    constexpr bool operator==(const FluidState& other) const noexcept = default;
};

} // namespace betacraft::fluids
