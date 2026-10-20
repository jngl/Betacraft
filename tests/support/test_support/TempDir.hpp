#pragma once

#include <chrono>
#include <filesystem>
#include <random>
#include <string>
#include <string_view>
#include <system_error>

namespace betacraft::test {

/// Temporary directory RAII helper for storage / world tests
class TempDir {
public:
    TempDir(std::string_view prefix = "betacraft_test_") {
        const auto tempRoot = std::filesystem::temp_directory_path();
        // Clock plus a random tag: UnitTests and AllTests run the same tests in parallel
        // processes, which must never share a directory.
        const auto uniqueName = std::string(prefix) + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                                "_" + std::to_string(std::random_device{}());
        m_path = tempRoot / uniqueName;
        std::filesystem::remove_all(m_path);
        std::filesystem::create_directories(m_path);
    }

    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(m_path, ec);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return m_path; }

private:
    std::filesystem::path m_path;
};

} // namespace betacraft::test
