#pragma once

#include "core/Types.hpp"
#include "render/GLHeaders.hpp"

#include <glm/glm.hpp>
#include <string>
#include <string_view>
#include <unordered_map>

namespace betacraft::render {

class ShaderProgram : private core::NonCopyable {
public:
    ShaderProgram() noexcept = default;
    ~ShaderProgram() noexcept;

    ShaderProgram(ShaderProgram&& other) noexcept;
    ShaderProgram& operator=(ShaderProgram&& other) noexcept;

    [[nodiscard]] bool compileFromSource(std::string_view vertexSrc, std::string_view fragmentSrc);

    void bind() const noexcept;
    static void unbind() noexcept;

    [[nodiscard]] GLuint getHandle() const noexcept { return m_program; }

    void setInt(const std::string& name, int value) const;
    void setFloat(const std::string& name, float value) const;
    void setVec2(const std::string& name, const glm::vec2& value) const;
    void setVec3(const std::string& name, const glm::vec3& value) const;
    void setIVec3(const std::string& name, const glm::ivec3& value) const;
    void setVec4(const std::string& name, const glm::vec4& value) const;
    void setMat4(const std::string& name, const glm::mat4& value) const;

private:
    [[nodiscard]] GLint getUniformLocation(const std::string& name) const;
    void destroy() noexcept;

    GLuint m_program{0};
    mutable std::unordered_map<std::string, GLint> m_uniformCache;
};

} // namespace betacraft::render
