#include "render/gl/Framebuffer.hpp"

#include <SDL3/SDL.h>

#include <utility>

namespace holobench::render::gl {

namespace {

struct GlBindingStateGuard {
    GLint prevDrawFbo = 0;
    GLint prevReadFbo = 0;
    GLint prevActiveTexture = GL_TEXTURE0;
    GLint prevActiveUnitTexture2D = 0;
    GLint prevUnit0Texture2D = 0;
    GLint prevRenderbuffer = 0;
    GLint prevPixelUnpackBuffer = 0;

    GlBindingStateGuard() noexcept {
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDrawFbo);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevReadFbo);
        glGetIntegerv(GL_RENDERBUFFER_BINDING, &prevRenderbuffer);
        glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &prevPixelUnpackBuffer);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &prevActiveTexture);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevActiveUnitTexture2D);

        if (static_cast<GLenum>(prevActiveTexture) != GL_TEXTURE0) {
            glActiveTexture(GL_TEXTURE0);
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevUnit0Texture2D);
        } else {
            prevUnit0Texture2D = prevActiveUnitTexture2D;
        }

        if (prevPixelUnpackBuffer != 0) {
            glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        }
    }

    ~GlBindingStateGuard() noexcept {
        if (static_cast<GLenum>(prevActiveTexture) != GL_TEXTURE0) {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevUnit0Texture2D));
            glActiveTexture(static_cast<GLenum>(prevActiveTexture));
            glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevActiveUnitTexture2D));
        } else {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevUnit0Texture2D));
        }

        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, static_cast<GLuint>(prevPixelUnpackBuffer));
        glBindRenderbuffer(GL_RENDERBUFFER, static_cast<GLuint>(prevRenderbuffer));
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(prevDrawFbo));
        glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(prevReadFbo));
    }

    void updateBindings(
        GLuint oldFbo,
        GLuint newFbo,
        GLuint oldColorTex,
        GLuint newColorTex,
        GLuint oldRbo,
        GLuint newRbo) noexcept {
        if (oldFbo != 0) {
            if (static_cast<GLuint>(prevDrawFbo) == oldFbo) {
                prevDrawFbo = static_cast<GLint>(newFbo);
            }
            if (static_cast<GLuint>(prevReadFbo) == oldFbo) {
                prevReadFbo = static_cast<GLint>(newFbo);
            }
        }
        if (oldColorTex != 0) {
            if (static_cast<GLuint>(prevActiveUnitTexture2D) == oldColorTex) {
                prevActiveUnitTexture2D = static_cast<GLint>(newColorTex);
            }
            if (static_cast<GLuint>(prevUnit0Texture2D) == oldColorTex) {
                prevUnit0Texture2D = static_cast<GLint>(newColorTex);
            }
        }
        if (oldRbo != 0 && static_cast<GLuint>(prevRenderbuffer) == oldRbo) {
            prevRenderbuffer = static_cast<GLint>(newRbo);
        }
    }

    GlBindingStateGuard(const GlBindingStateGuard&) = delete;
    GlBindingStateGuard& operator=(const GlBindingStateGuard&) = delete;
    GlBindingStateGuard(GlBindingStateGuard&&) = delete;
    GlBindingStateGuard& operator=(GlBindingStateGuard&&) = delete;
};

struct FboGuard {
    GLuint id = 0;
    ~FboGuard() noexcept {
        if (id != 0) {
            glDeleteFramebuffers(1, &id);
        }
    }

    [[nodiscard]] GLuint release() noexcept {
        return std::exchange(id, 0);
    }
};

struct TextureGuard {
    GLuint id = 0;
    ~TextureGuard() noexcept {
        if (id != 0) {
            glDeleteTextures(1, &id);
        }
    }

    [[nodiscard]] GLuint release() noexcept {
        return std::exchange(id, 0);
    }
};

struct RenderbufferGuard {
    GLuint id = 0;
    ~RenderbufferGuard() noexcept {
        if (id != 0) {
            glDeleteRenderbuffers(1, &id);
        }
    }

    [[nodiscard]] GLuint release() noexcept {
        return std::exchange(id, 0);
    }
};

[[nodiscard]] const char* framebufferStatusToString(GLenum status) noexcept {
    switch (status) {
    case GL_FRAMEBUFFER_COMPLETE:
        return "GL_FRAMEBUFFER_COMPLETE";
    case GL_FRAMEBUFFER_UNDEFINED:
        return "GL_FRAMEBUFFER_UNDEFINED";
    case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:
        return "GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT";
    case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT:
        return "GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT";
    case GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER:
        return "GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER";
    case GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER:
        return "GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER";
    case GL_FRAMEBUFFER_UNSUPPORTED:
        return "GL_FRAMEBUFFER_UNSUPPORTED";
    case GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE:
        return "GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE";
    case GL_FRAMEBUFFER_INCOMPLETE_LAYER_TARGETS:
        return "GL_FRAMEBUFFER_INCOMPLETE_LAYER_TARGETS";
    default:
        return "GL_FRAMEBUFFER_STATUS_UNKNOWN";
    }
}

} // namespace

Framebuffer::~Framebuffer() {
    destroy();
}

Framebuffer::Framebuffer(Framebuffer&& other) noexcept
    : fbo_(std::exchange(other.fbo_, 0))
    , colorTexture_(std::exchange(other.colorTexture_, 0))
    , rbo_(std::exchange(other.rbo_, 0))
    , width_(std::exchange(other.width_, 0))
    , height_(std::exchange(other.height_, 0)) {}

Framebuffer& Framebuffer::operator=(Framebuffer&& other) noexcept {
    if (this != &other) {
        destroy();
        fbo_ = std::exchange(other.fbo_, 0);
        colorTexture_ = std::exchange(other.colorTexture_, 0);
        rbo_ = std::exchange(other.rbo_, 0);
        width_ = std::exchange(other.width_, 0);
        height_ = std::exchange(other.height_, 0);
    }
    return *this;
}

void Framebuffer::destroy() noexcept {
    if (rbo_ != 0) {
        glDeleteRenderbuffers(1, &rbo_);
        rbo_ = 0;
    }
    if (colorTexture_ != 0) {
        glDeleteTextures(1, &colorTexture_);
        colorTexture_ = 0;
    }
    if (fbo_ != 0) {
        glDeleteFramebuffers(1, &fbo_);
        fbo_ = 0;
    }
    width_ = 0;
    height_ = 0;
}

bool Framebuffer::resize(int width, int height) {
    if (width <= 0 || height <= 0) {
        SDL_LogError(
            SDL_LOG_CATEGORY_RENDER,
            "Framebuffer::resize failed: invalid dimensions %dx%d (dimensions must be positive).",
            width,
            height);
        return false;
    }

    if (width == width_ && height == height_ && isValid()) {
        return true;
    }

    // Preserve caller's GL bindings so we do not pollute external OpenGL state.
    GlBindingStateGuard bindingGuard;

    FboGuard newFbo;
    glGenFramebuffers(1, &newFbo.id);
    if (newFbo.id == 0) {
        SDL_LogError(
            SDL_LOG_CATEGORY_RENDER,
            "Framebuffer::resize failed: unable to allocate OpenGL framebuffer object for size %dx%d.",
            width,
            height);
        return false;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, newFbo.id);

    TextureGuard newColorTex;
    glGenTextures(1, &newColorTex.id);
    if (newColorTex.id == 0) {
        SDL_LogError(
            SDL_LOG_CATEGORY_RENDER,
            "Framebuffer::resize failed: unable to allocate OpenGL texture object for size %dx%d.",
            width,
            height);
        return false;
    }
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, newColorTex.id);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA8,
        width,
        height,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(
        GL_FRAMEBUFFER,
        GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_2D,
        newColorTex.id,
        0);

    RenderbufferGuard newRbo;
    glGenRenderbuffers(1, &newRbo.id);
    if (newRbo.id == 0) {
        SDL_LogError(
            SDL_LOG_CATEGORY_RENDER,
            "Framebuffer::resize failed: unable to allocate OpenGL renderbuffer object for size %dx%d.",
            width,
            height);
        return false;
    }
    glBindRenderbuffer(GL_RENDERBUFFER, newRbo.id);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
    glFramebufferRenderbuffer(
        GL_FRAMEBUFFER,
        GL_DEPTH_STENCIL_ATTACHMENT,
        GL_RENDERBUFFER,
        newRbo.id);

    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        SDL_LogError(
            SDL_LOG_CATEGORY_RENDER,
            "Framebuffer::resize failed: framebuffer is incomplete (%s [0x%04X]) for size %dx%d.",
            framebufferStatusToString(status),
            static_cast<unsigned int>(status),
            width,
            height);
        return false;
    }

    const GLuint oldFbo = fbo_;
    const GLuint oldColorTex = colorTexture_;
    const GLuint oldRbo = rbo_;

    // New resources are valid and complete: destroy previous resources and transfer ownership.
    destroy();

    fbo_ = newFbo.release();
    colorTexture_ = newColorTex.release();
    rbo_ = newRbo.release();
    width_ = width;
    height_ = height;

    bindingGuard.updateBindings(oldFbo, fbo_, oldColorTex, colorTexture_, oldRbo, rbo_);

    return true;
}

void Framebuffer::bind() const noexcept {
    if (fbo_ != 0) {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    }
}

void Framebuffer::unbind() const noexcept {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

} // namespace holobench::render::gl

