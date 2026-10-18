#pragma once

#include "core/Types.hpp"
#include "lighting/DynamicLightUbo.hpp"
#include "render/GLHeaders.hpp"

namespace betacraft::lighting {

// Thin OpenGL owner of the DynamicLightBlock UBO. Requires a current GL 3.3 context and
// render::initOpenGLFunctions() to have succeeded. Deliberately NOT exercised by unit tests:
// all logic worth testing lives in the CPU-side packer/culler; this class only moves bytes.
//
// Per frame (render thread):
//   ubo.upload(frame.uboBytes());   // glBufferSubData, skipped if bytes are unchanged
//   ubo.bindBase();                 // glBindBufferBase(GL_UNIFORM_BUFFER, kUboBindingPoint, ...)
// Once per linked chunk program:
//   DynamicLightUniformBuffer::bindProgram(program.getHandle());
class DynamicLightUniformBuffer : private core::NonCopyable {
public:
    DynamicLightUniformBuffer() noexcept = default;
    ~DynamicLightUniformBuffer() noexcept;

    // Loads the UBO entry points (kept local to this module, GLHeaders is not extended) and
    // allocates the GPU buffer. Returns false if the driver lacks uniform buffer support.
    [[nodiscard]] bool initialize() noexcept;

    // Points the program's DynamicLightBlock at kUboBindingPoint. Returns false when the
    // program does not declare the block (e.g. the include was not injected).
    [[nodiscard]] static bool bindProgram(GLuint program) noexcept;

    void upload(const UboBuffer& bytes) noexcept;
    void bindBase() const noexcept;
    void destroy() noexcept;

    [[nodiscard]] bool isInitialized() const noexcept { return m_ubo != 0; }

private:
    GLuint m_ubo{0};
    UboBuffer m_lastUploaded{};
    bool m_hasUpload{false};
};

} // namespace betacraft::lighting
