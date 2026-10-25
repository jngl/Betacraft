#pragma once

// Shared fixtures for the streaming test-suite. Kept free of SDL/GL so the
// suite runs headless (and under ThreadSanitizer) on CI machines.

#include "voxel/Block.hpp"
#include "voxel/SectionCoord.hpp"
#include "world/World.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <ostream>
#include <string>
#include <system_error>

namespace betacraft::streaming {

// Readable gtest diagnostics for SectionCoord (found by ADL).
inline void PrintTo(const voxel::SectionCoord& c, std::ostream* os) { *os << "(" << c.x << "," << c.y << "," << c.z << ")"; }

} // namespace betacraft::streaming

namespace betacraft::streaming::test {

// RAII temporary directory: World needs a save path even when nothing is persisted.
class ScopedTempDir {
public:
    ScopedTempDir() {
        static std::atomic<uint64_t> s_counter{0};
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        m_path = std::filesystem::temp_directory_path() /
                 ("betacraft_streaming_" + std::to_string(stamp) + "_" + std::to_string(s_counter.fetch_add(1)));
        std::filesystem::create_directories(m_path);
    }
    ~ScopedTempDir() {
        std::error_code ec;
        std::filesystem::remove_all(m_path, ec);
    }
    ScopedTempDir(const ScopedTempDir&) = delete;
    ScopedTempDir& operator=(const ScopedTempDir&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return m_path; }

private:
    std::filesystem::path m_path;
};

// Deterministic fake monotonic clock: tests advance it explicitly.
class FakeClock {
public:
    [[nodiscard]] std::chrono::nanoseconds now() const noexcept { return m_now; }
    void advance(std::chrono::nanoseconds d) noexcept { m_now += d; }

private:
    std::chrono::nanoseconds m_now{0};
};

inline constexpr std::chrono::seconds kGenerousTimeout{10};

} // namespace betacraft::streaming::test
