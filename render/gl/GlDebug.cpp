#include "render/gl/GlDebug.hpp"

#include <glad/gl.h>
#include <SDL3/SDL.h>

#include <atomic>

namespace holobench::render::gl {
namespace {

std::atomic<std::uint32_t> debugErrorCount = 0;

void GLAD_API_PTR debugCallback(
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
    if (glDebugMessageCallback == nullptr) {
        SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "OpenGL debug callback is unavailable");
        return;
    }

    glEnable(GL_DEBUG_OUTPUT);
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    glDebugMessageCallback(debugCallback, nullptr);
}

std::uint32_t errorCount() noexcept {
    return debugErrorCount.load(std::memory_order_relaxed);
}

} // namespace holobench::render::gl
