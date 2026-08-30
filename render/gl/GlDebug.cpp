#include "render/gl/GlDebug.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#include <atomic>

namespace holobench::render::gl {
namespace {

std::atomic<std::uint32_t> debugErrorCount = 0;

void APIENTRY debugCallback(
    GLenum,
    GLenum type,
    GLuint,
    GLenum severity,
    GLsizei,
    const GLchar* message,
    const void*) {
    if (severity == GL_DEBUG_SEVERITY_NOTIFICATION) {
        return;
    }
    if (type == GL_DEBUG_TYPE_ERROR) {
        debugErrorCount.fetch_add(1, std::memory_order_relaxed);
    }
    const SDL_LogPriority priority = type == GL_DEBUG_TYPE_ERROR ? SDL_LOG_PRIORITY_ERROR : SDL_LOG_PRIORITY_WARN;
    SDL_LogMessage(SDL_LOG_CATEGORY_RENDER, priority, "OpenGL: %s", message);
}

} // namespace

void installDebugCallback() {
    const auto callback = reinterpret_cast<PFNGLDEBUGMESSAGECALLBACKPROC>(SDL_GL_GetProcAddress("glDebugMessageCallback"));
    if (callback == nullptr) {
        SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "OpenGL debug callback is unavailable");
        return;
    }

    glEnable(GL_DEBUG_OUTPUT);
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    callback(debugCallback, nullptr);
}

std::uint32_t errorCount() noexcept {
    return debugErrorCount.load(std::memory_order_relaxed);
}

} // namespace holobench::render::gl
