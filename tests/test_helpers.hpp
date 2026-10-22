#pragma once

#include "core/Config.hpp"
#include "render/GLHeaders.hpp"
#include "test_support/TempDir.hpp"

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <SDL2/SDL.h>

namespace betacraft::test {

/// OpenGL Test Context helper (creates hidden window + GL 3.3 Core context)
class GLTestContext {
public:
    static bool isAvailable() noexcept {
        initOnce();
        return s_available;
    }

private:
    static void initOnce() noexcept {
        if (s_initialized) return;
        s_initialized = true;

        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
            s_available = false;
            return;
        }

        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

        s_window = SDL_CreateWindow(
            "BetaCraftTestGL",
            SDL_WINDOWPOS_CENTERED,
            SDL_WINDOWPOS_CENTERED,
            64, 64,
            SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN
        );
        if (!s_window) {
            s_available = false;
            return;
        }

        s_glContext = SDL_GL_CreateContext(s_window);
        if (!s_glContext) {
            SDL_DestroyWindow(s_window);
            s_window = nullptr;
            s_available = false;
            return;
        }

        SDL_GL_MakeCurrent(s_window, s_glContext);
        s_available = render::initOpenGLFunctions();
    }

    inline static bool s_initialized{false};
    inline static bool s_available{false};
    inline static SDL_Window* s_window{nullptr};
    inline static SDL_GLContext s_glContext{nullptr};
};

} // namespace betacraft::test
