#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace betacraft::test::storage {

// Incompressible bytes (xorshift), so a payload takes the same sectors with or without zstd.
[[nodiscard]] inline std::vector<uint8_t> noise(size_t size, uint32_t seed) {
    std::vector<uint8_t> out(size);
    uint32_t x = seed * 2654435761U + 1U;
    for (auto& b : out) {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        b = static_cast<uint8_t>(x >> 24);
    }
    return out;
}

[[nodiscard]] inline std::vector<uint8_t> readBytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

inline void writeBytes(const std::filesystem::path& path, const std::vector<uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

inline void writeText(const std::filesystem::path& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
}

[[nodiscard]] inline std::string readText(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

} // namespace betacraft::test::storage
