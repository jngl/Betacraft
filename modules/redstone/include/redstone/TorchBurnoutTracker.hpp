#pragma once

#include "core/Types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <unordered_map>

namespace betacraft::redstone {

// Redstone torch burnout (Beta BlockRedstoneTorch.checkForBurnout, with our own
// numbers): a torch that switches more than kMaxToggles times within kWindowTicks burns out
// and stays off for kBurnoutTicks, so a too-fast clock stops instead of eating every tick.
// Not persisted: a reloaded chunk starts with a clean history.
class TorchBurnoutTracker {
public:
    static constexpr uint64_t kWindowTicks = 60;
    static constexpr size_t kMaxToggles = 8;
    static constexpr uint64_t kBurnoutTicks = 60;

    // Whether the torch at `pos` may switch at `now`. When it may, the switch is recorded;
    // when it would be one too many within the window, the torch burns out until
    // now + kBurnoutTicks and false is returned (the caller leaves it, or turns it, off).
    bool tryToggle(const core::BlockPos& pos, uint64_t now) {
        Entry& e = m_entries[pos];
        prune(e, now);
        if (e.count >= kMaxToggles) {
            e.burntUntil = now + kBurnoutTicks;
            e.count = 0;
            return false;
        }
        e.times[(e.first + e.count) % e.times.size()] = now;
        ++e.count;
        return true;
    }

    [[nodiscard]] bool isBurntOut(const core::BlockPos& pos, uint64_t now) const {
        const auto it = m_entries.find(pos);
        return it != m_entries.end() && now < it->second.burntUntil;
    }

    // Tick the burnout of `pos` ends (0 when it never burnt out).
    [[nodiscard]] uint64_t burntUntil(const core::BlockPos& pos) const {
        const auto it = m_entries.find(pos);
        return it != m_entries.end() ? it->second.burntUntil : 0;
    }

    // Switches recorded within the window ending at `now`.
    [[nodiscard]] size_t recentToggles(const core::BlockPos& pos, uint64_t now) const {
        const auto it = m_entries.find(pos);
        if (it == m_entries.end()) return 0;
        size_t n = 0;
        for (size_t i = 0; i < it->second.count; ++i) {
            if (now - it->second.times[(it->second.first + i) % it->second.times.size()] < kWindowTicks) ++n;
        }
        return n;
    }

    void forget(const core::BlockPos& pos) { m_entries.erase(pos); }
    void clear() noexcept { m_entries.clear(); }
    [[nodiscard]] size_t size() const noexcept { return m_entries.size(); }

    // Drops the torches with nothing left to remember at `now`.
    void collect(uint64_t now) {
        for (auto it = m_entries.begin(); it != m_entries.end();) {
            prune(it->second, now);
            if (it->second.count == 0 && now >= it->second.burntUntil) {
                it = m_entries.erase(it);
            } else {
                ++it;
            }
        }
    }

private:
    struct Entry {
        std::array<uint64_t, kMaxToggles> times{};
        size_t first{0};
        size_t count{0};
        uint64_t burntUntil{0};
    };

    static void prune(Entry& e, uint64_t now) noexcept {
        while (e.count > 0 && now - e.times[e.first] >= kWindowTicks) {
            e.first = (e.first + 1) % e.times.size();
            --e.count;
        }
    }

    std::unordered_map<core::BlockPos, Entry> m_entries;
};

} // namespace betacraft::redstone
