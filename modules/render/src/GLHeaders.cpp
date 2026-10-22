#include "render/GLHeaders.hpp"
#include "core/Logger.hpp"

#include <cstring>

PFNGLCREATESHADERPROC bc_glCreateShader = nullptr;
PFNGLSHADERSOURCEPROC bc_glShaderSource = nullptr;
PFNGLCOMPILESHADERPROC bc_glCompileShader = nullptr;
PFNGLGETSHADERIVPROC bc_glGetShaderiv = nullptr;
PFNGLGETSHADERINFOLOGPROC bc_glGetShaderInfoLog = nullptr;
PFNGLDELETESHADERPROC bc_glDeleteShader = nullptr;
PFNGLCREATEPROGRAMPROC bc_glCreateProgram = nullptr;
PFNGLATTACHSHADERPROC bc_glAttachShader = nullptr;
PFNGLLINKPROGRAMPROC bc_glLinkProgram = nullptr;
PFNGLGETPROGRAMIVPROC bc_glGetProgramiv = nullptr;
PFNGLGETPROGRAMINFOLOGPROC bc_glGetProgramInfoLog = nullptr;
PFNGLUSEPROGRAMPROC bc_glUseProgram = nullptr;
PFNGLDELETEPROGRAMPROC bc_glDeleteProgram = nullptr;
PFNGLGETUNIFORMLOCATIONPROC bc_glGetUniformLocation = nullptr;
PFNGLUNIFORM1IPROC bc_glUniform1i = nullptr;
PFNGLUNIFORM3IPROC bc_glUniform3i = nullptr;
PFNGLUNIFORM1FPROC bc_glUniform1f = nullptr;
PFNGLUNIFORM2FPROC bc_glUniform2f = nullptr;
PFNGLUNIFORM3FPROC bc_glUniform3f = nullptr;
PFNGLUNIFORM3FVPROC bc_glUniform3fv = nullptr;
PFNGLUNIFORM4FPROC bc_glUniform4f = nullptr;
PFNGLUNIFORMMATRIX4FVPROC bc_glUniformMatrix4fv = nullptr;
PFNGLGENVERTEXARRAYSPROC bc_glGenVertexArrays = nullptr;
PFNGLBINDVERTEXARRAYPROC bc_glBindVertexArray = nullptr;
PFNGLDELETEVERTEXARRAYSPROC bc_glDeleteVertexArrays = nullptr;
PFNGLGENBUFFERSPROC bc_glGenBuffers = nullptr;
PFNGLBINDBUFFERPROC bc_glBindBuffer = nullptr;
PFNGLBUFFERDATAPROC bc_glBufferData = nullptr;
PFNGLBUFFERSUBDATAPROC bc_glBufferSubData = nullptr;
PFNGLDELETEBUFFERSPROC bc_glDeleteBuffers = nullptr;
PFNGLENABLEVERTEXATTRIBARRAYPROC bc_glEnableVertexAttribArray = nullptr;
PFNGLVERTEXATTRIBPOINTERPROC bc_glVertexAttribPointer = nullptr;
PFNGLVERTEXATTRIBIPOINTERPROC bc_glVertexAttribIPointer = nullptr;
PFNGLACTIVETEXTUREPROC bc_glActiveTexture = nullptr;
PFNGLGENERATEMIPMAPPROC bc_glGenerateMipmap = nullptr;

namespace betacraft::render {

namespace {

template <typename FnPtr>
bool loadGLProc(const char* name, FnPtr& outFn) noexcept {
    void* raw = SDL_GL_GetProcAddress(name);
    if (!raw) {
        core::Logger::error("OpenGL", "Failed to load GL function: ", name);
        return false;
    }
    std::memcpy(&outFn, &raw, sizeof(FnPtr));
    return true;
}

} // namespace

bool initOpenGLFunctions() noexcept {
    bool ok = true;
    ok &= loadGLProc("glCreateShader", bc_glCreateShader);
    ok &= loadGLProc("glShaderSource", bc_glShaderSource);
    ok &= loadGLProc("glCompileShader", bc_glCompileShader);
    ok &= loadGLProc("glGetShaderiv", bc_glGetShaderiv);
    ok &= loadGLProc("glGetShaderInfoLog", bc_glGetShaderInfoLog);
    ok &= loadGLProc("glDeleteShader", bc_glDeleteShader);
    ok &= loadGLProc("glCreateProgram", bc_glCreateProgram);
    ok &= loadGLProc("glAttachShader", bc_glAttachShader);
    ok &= loadGLProc("glLinkProgram", bc_glLinkProgram);
    ok &= loadGLProc("glGetProgramiv", bc_glGetProgramiv);
    ok &= loadGLProc("glGetProgramInfoLog", bc_glGetProgramInfoLog);
    ok &= loadGLProc("glUseProgram", bc_glUseProgram);
    ok &= loadGLProc("glDeleteProgram", bc_glDeleteProgram);
    ok &= loadGLProc("glGetUniformLocation", bc_glGetUniformLocation);
    ok &= loadGLProc("glUniform1i", bc_glUniform1i);
    ok &= loadGLProc("glUniform3i", bc_glUniform3i);
    ok &= loadGLProc("glUniform1f", bc_glUniform1f);
    ok &= loadGLProc("glUniform2f", bc_glUniform2f);
    ok &= loadGLProc("glUniform3f", bc_glUniform3f);
    ok &= loadGLProc("glUniform3fv", bc_glUniform3fv);
    ok &= loadGLProc("glUniform4f", bc_glUniform4f);
    ok &= loadGLProc("glUniformMatrix4fv", bc_glUniformMatrix4fv);
    ok &= loadGLProc("glGenVertexArrays", bc_glGenVertexArrays);
    ok &= loadGLProc("glBindVertexArray", bc_glBindVertexArray);
    ok &= loadGLProc("glDeleteVertexArrays", bc_glDeleteVertexArrays);
    ok &= loadGLProc("glGenBuffers", bc_glGenBuffers);
    ok &= loadGLProc("glBindBuffer", bc_glBindBuffer);
    ok &= loadGLProc("glBufferData", bc_glBufferData);
    ok &= loadGLProc("glBufferSubData", bc_glBufferSubData);
    ok &= loadGLProc("glDeleteBuffers", bc_glDeleteBuffers);
    ok &= loadGLProc("glEnableVertexAttribArray", bc_glEnableVertexAttribArray);
    ok &= loadGLProc("glVertexAttribPointer", bc_glVertexAttribPointer);
    ok &= loadGLProc("glVertexAttribIPointer", bc_glVertexAttribIPointer);
    ok &= loadGLProc("glActiveTexture", bc_glActiveTexture);
    ok &= loadGLProc("glGenerateMipmap", bc_glGenerateMipmap);
    return ok;
}

} // namespace betacraft::render
