#include "render/DynamicLightUniformBuffer.hpp"

#include "core/Logger.hpp"

#include <cstring>

namespace betacraft::render {

namespace {

// Module-local GL 3.1+ uniform-buffer entry points (not part of render/GLHeaders).
PFNGLBINDBUFFERBASEPROC s_glBindBufferBase = nullptr;
PFNGLGETUNIFORMBLOCKINDEXPROC s_glGetUniformBlockIndex = nullptr;
PFNGLUNIFORMBLOCKBINDINGPROC s_glUniformBlockBinding = nullptr;

template <typename FnPtr>
bool loadProc(const char* name, FnPtr& out) noexcept {
    void* raw = SDL_GL_GetProcAddress(name);
    if (raw == nullptr) {
        core::Logger::error("DynamicLights", "Failed to load GL function: ", name);
        return false;
    }
    std::memcpy(&out, &raw, sizeof(FnPtr));
    return true;
}

[[nodiscard]] bool loadUboProcs() noexcept {
    bool ok = true;
    ok &= loadProc("glBindBufferBase", s_glBindBufferBase);
    ok &= loadProc("glGetUniformBlockIndex", s_glGetUniformBlockIndex);
    ok &= loadProc("glUniformBlockBinding", s_glUniformBlockBinding);
    return ok;
}

} // namespace

DynamicLightUniformBuffer::~DynamicLightUniformBuffer() noexcept {
    destroy();
}

bool DynamicLightUniformBuffer::initialize() noexcept {
    if (m_ubo != 0) {
        return true;
    }
    if (!loadUboProcs() || bc_glGenBuffers == nullptr) {
        return false;
    }
    bc_glGenBuffers(1, &m_ubo);
    bc_glBindBuffer(GL_UNIFORM_BUFFER, m_ubo);
    // Allocate once; per-frame updates use glBufferSubData to avoid buffer orphaning churn.
    bc_glBufferData(GL_UNIFORM_BUFFER, static_cast<GLsizeiptr>(lighting::ubo::kBlockSize), nullptr, GL_DYNAMIC_DRAW);
    bc_glBindBuffer(GL_UNIFORM_BUFFER, 0);
    m_hasUpload = false;

    // Start from a valid empty block (count = 0) so a shader sampling it before the first
    // frame reads no garbage.
    lighting::UboBuffer empty{};
    lighting::packDynamicLightUbo({}, {}, glm::vec2(1.0f), empty);
    upload(empty);
    bindBase();
    return true;
}

bool DynamicLightUniformBuffer::bindProgram(GLuint program) noexcept {
    if (program == 0 || s_glGetUniformBlockIndex == nullptr || s_glUniformBlockBinding == nullptr) {
        return false;
    }
    const GLuint index = s_glGetUniformBlockIndex(program, lighting::kUboBlockName);
    if (index == GL_INVALID_INDEX) {
        return false;
    }
    s_glUniformBlockBinding(program, index, lighting::kUboBindingPoint);
    return true;
}

void DynamicLightUniformBuffer::upload(const lighting::UboBuffer& bytes) noexcept {
    if (m_ubo == 0) {
        return;
    }
    // Typical frames (standing still with a torch) produce identical bytes: skip the copy.
    if (m_hasUpload && std::memcmp(m_lastUploaded.data(), bytes.data(), bytes.size()) == 0) {
        return;
    }
    bc_glBindBuffer(GL_UNIFORM_BUFFER, m_ubo);
    bc_glBufferSubData(GL_UNIFORM_BUFFER, 0, static_cast<GLsizeiptr>(bytes.size()), bytes.data());
    bc_glBindBuffer(GL_UNIFORM_BUFFER, 0);
    m_lastUploaded = bytes;
    m_hasUpload = true;
}

void DynamicLightUniformBuffer::bindBase() const noexcept {
    if (m_ubo != 0 && s_glBindBufferBase != nullptr) {
        s_glBindBufferBase(GL_UNIFORM_BUFFER, lighting::kUboBindingPoint, m_ubo);
    }
}

void DynamicLightUniformBuffer::destroy() noexcept {
    if (m_ubo != 0 && bc_glDeleteBuffers != nullptr) {
        bc_glDeleteBuffers(1, &m_ubo);
    }
    m_ubo = 0;
    m_hasUpload = false;
}

} // namespace betacraft::render
