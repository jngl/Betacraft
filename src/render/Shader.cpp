#include "render/Shader.hpp"
#include "core/Logger.hpp"

#include <glm/gtc/type_ptr.hpp>
#include <vector>

namespace betacraft::render {

namespace {

GLuint compileSingleShader(GLenum type, std::string_view src) {
    const GLuint shader = bc_glCreateShader(type);
    const char* ptr = src.data();
    const GLint len = static_cast<GLint>(src.size());
    bc_glShaderSource(shader, 1, &ptr, &len);
    bc_glCompileShader(shader);

    GLint compiled = 0;
    bc_glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        GLint logLen = 0;
        bc_glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLen);
        std::vector<char> infoLog(static_cast<size_t>(std::max(1, logLen)));
        bc_glGetShaderInfoLog(shader, logLen, nullptr, infoLog.data());
        core::Logger::error("Shader", "Shader compilation error:\n", infoLog.data());
        bc_glDeleteShader(shader);
        return 0;
    }
    return shader;
}

} // namespace

ShaderProgram::~ShaderProgram() noexcept {
    destroy();
}

ShaderProgram::ShaderProgram(ShaderProgram&& other) noexcept
    : m_program(other.m_program), m_uniformCache(std::move(other.m_uniformCache)) {
    other.m_program = 0;
}

ShaderProgram& ShaderProgram::operator=(ShaderProgram&& other) noexcept {
    if (this != &other) {
        destroy();
        m_program = other.m_program;
        m_uniformCache = std::move(other.m_uniformCache);
        other.m_program = 0;
    }
    return *this;
}

void ShaderProgram::destroy() noexcept {
    if (m_program != 0 && bc_glDeleteProgram) {
        bc_glDeleteProgram(m_program);
        m_program = 0;
    }
    m_uniformCache.clear();
}

bool ShaderProgram::compileFromSource(std::string_view vertexSrc, std::string_view fragmentSrc) {
    destroy();

    const GLuint vs = compileSingleShader(GL_VERTEX_SHADER, vertexSrc);
    if (vs == 0) return false;

    const GLuint fs = compileSingleShader(GL_FRAGMENT_SHADER, fragmentSrc);
    if (fs == 0) {
        bc_glDeleteShader(vs);
        return false;
    }

    const GLuint prog = bc_glCreateProgram();
    bc_glAttachShader(prog, vs);
    bc_glAttachShader(prog, fs);
    bc_glLinkProgram(prog);

    bc_glDeleteShader(vs);
    bc_glDeleteShader(fs);

    GLint linked = 0;
    bc_glGetProgramiv(prog, GL_LINK_STATUS, &linked);
    if (!linked) {
        GLint logLen = 0;
        bc_glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &logLen);
        std::vector<char> infoLog(static_cast<size_t>(std::max(1, logLen)));
        bc_glGetProgramInfoLog(prog, logLen, nullptr, infoLog.data());
        core::Logger::error("Shader", "Shader link error:\n", infoLog.data());
        bc_glDeleteProgram(prog);
        return false;
    }

    m_program = prog;
    return true;
}

void ShaderProgram::bind() const noexcept {
    bc_glUseProgram(m_program);
}

void ShaderProgram::unbind() noexcept {
    bc_glUseProgram(0);
}

GLint ShaderProgram::getUniformLocation(const std::string& name) const {
    auto it = m_uniformCache.find(name);
    if (it != m_uniformCache.end()) {
        return it->second;
    }
    const GLint loc = bc_glGetUniformLocation(m_program, name.c_str());
    m_uniformCache.emplace(name, loc);
    return loc;
}

void ShaderProgram::setInt(const std::string& name, int value) const {
    const GLint loc = getUniformLocation(name);
    if (loc >= 0) bc_glUniform1i(loc, value);
}

void ShaderProgram::setFloat(const std::string& name, float value) const {
    const GLint loc = getUniformLocation(name);
    if (loc >= 0) bc_glUniform1f(loc, value);
}

void ShaderProgram::setVec2(const std::string& name, const glm::vec2& value) const {
    const GLint loc = getUniformLocation(name);
    if (loc >= 0) bc_glUniform2f(loc, value.x, value.y);
}

void ShaderProgram::setVec3(const std::string& name, const glm::vec3& value) const {
    const GLint loc = getUniformLocation(name);
    if (loc >= 0) bc_glUniform3f(loc, value.x, value.y, value.z);
}

void ShaderProgram::setIVec3(const std::string& name, const glm::ivec3& value) const {
    const GLint loc = getUniformLocation(name);
    if (loc >= 0 && bc_glUniform3i) bc_glUniform3i(loc, value.x, value.y, value.z);
}

void ShaderProgram::setVec4(const std::string& name, const glm::vec4& value) const {
    const GLint loc = getUniformLocation(name);
    if (loc >= 0) bc_glUniform4f(loc, value.x, value.y, value.z, value.w);
}

void ShaderProgram::setMat4(const std::string& name, const glm::mat4& value) const {
    const GLint loc = getUniformLocation(name);
    if (loc >= 0) bc_glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(value));
}

} // namespace betacraft::render
