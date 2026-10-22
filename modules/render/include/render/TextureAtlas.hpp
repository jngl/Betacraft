#pragma once

#include "core/Types.hpp"
#include "render/GLHeaders.hpp"

#include <cstdint>
#include <filesystem>
#include <glm/glm.hpp>
#include <vector>

namespace betacraft::render {

class TextureAtlas : private core::NonCopyable {
public:
    static constexpr int32_t ATLAS_SIZE = 256;
    static constexpr int32_t TILE_SIZE = 16;
    static constexpr int32_t TILES_PER_ROW = ATLAS_SIZE / TILE_SIZE;

    TextureAtlas() noexcept = default;
    ~TextureAtlas() noexcept;

    [[nodiscard]] bool initialize(const std::filesystem::path& externalPngPath = "assets/terrain.png");

    void bind(GLuint unit = 0) const noexcept;
    [[nodiscard]] GLuint getHandle() const noexcept { return m_textureId; }

    [[nodiscard]] static glm::vec4 getTileUV(uint8_t tileIndex) noexcept;

    // Exposed for unit testing and PNG export
    [[nodiscard]] static std::vector<uint8_t> generateProceduralAtlasRGBA();

    // Copies `procedural` tiles over every tile of `loaded` that is still the flat grey
    // placeholder, so icons added after an atlas PNG was exported (tools, lava) show up.
    // Both images must be ATLAS_SIZE^2 RGBA. Returns the number of tiles filled.
    static size_t fillPlaceholderTiles(std::vector<uint8_t>& loaded, const std::vector<uint8_t>& procedural);
    [[nodiscard]] static bool isPlaceholderTile(const std::vector<uint8_t>& rgba, uint8_t tile);

private:
    [[nodiscard]] static bool loadPngRGBA(const std::filesystem::path& path, std::vector<uint8_t>& outRgba);
    static bool savePngRGBA(const std::filesystem::path& path, const std::vector<uint8_t>& rgba);

    GLuint m_textureId{0};
};

} // namespace betacraft::render
