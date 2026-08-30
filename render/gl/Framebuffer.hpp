#pragma once

#include <glad/gl.h>

namespace holobench::render::gl {

class Framebuffer final {
public:
    Framebuffer() noexcept = default;
    ~Framebuffer();

    Framebuffer(const Framebuffer&) = delete;
    Framebuffer& operator=(const Framebuffer&) = delete;

    Framebuffer(Framebuffer&& other) noexcept;
    Framebuffer& operator=(Framebuffer&& other) noexcept;

    [[nodiscard]] bool resize(int width, int height);
    void destroy() noexcept;

    void bind() const noexcept;
    void unbind() const noexcept;

    [[nodiscard]] GLuint colorTextureId() const noexcept {
        return colorTexture_;
    }

    [[nodiscard]] GLuint depthStencilRboId() const noexcept {
        return rbo_;
    }

    [[nodiscard]] GLuint handle() const noexcept {
        return fbo_;
    }

    [[nodiscard]] int width() const noexcept {
        return width_;
    }

    [[nodiscard]] int height() const noexcept {
        return height_;
    }

    [[nodiscard]] bool isValid() const noexcept {
        return fbo_ != 0 && colorTexture_ != 0 && rbo_ != 0;
    }

private:
    GLuint fbo_ = 0;
    GLuint colorTexture_ = 0;
    GLuint rbo_ = 0;
    int width_ = 0;
    int height_ = 0;
};

} // namespace holobench::render::gl

