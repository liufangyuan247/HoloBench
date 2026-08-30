#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include <glad/gl.h>

#include "core/field/FieldVisualization.hpp"

namespace holobench::render::gl {

/**
 * @brief RAII wrapper for an OpenGL 2D Texture object.
 *
 * Every member function, including destruction of a valid texture, must run
 * on the thread that owns the active OpenGL context.  The wrapper therefore
 * has to be destroyed before the application tears that context down.
 */
class Texture2D final {
public:
    Texture2D() noexcept = default;
    ~Texture2D();

    Texture2D(const Texture2D&) = delete;
    Texture2D& operator=(const Texture2D&) = delete;

    Texture2D(Texture2D&& other) noexcept;
    Texture2D& operator=(Texture2D&& other) noexcept;

    void destroy() noexcept;

    /**
     * @brief Uploads or updates 8-bit RGBA pixel data.
     *
     * @param width Image width in pixels.
     * @param height Image height in pixels.
     * @param rgbaBytes Span containing width * height * 4 raw bytes.
     * @return true on success, false if dimensions or data are invalid.
     * @note Requires an active OpenGL context on the calling thread.
     */
    [[nodiscard]] bool uploadRgba8(int width, int height, std::span<const std::uint8_t> rgbaBytes);

    /**
     * @brief Uploads pixel buffer from an RgbaImage.
     */
    [[nodiscard]] bool uploadImage(const field::RgbaImage& image);

    void bind(GLuint unit = 0) const noexcept;
    void unbind(GLuint unit = 0) const noexcept;

    [[nodiscard]] GLuint handle() const noexcept { return textureId_; }
    [[nodiscard]] int width() const noexcept { return width_; }
    [[nodiscard]] int height() const noexcept { return height_; }
    [[nodiscard]] bool isValid() const noexcept { return textureId_ != 0; }

private:
    GLuint textureId_ = 0;
    int width_ = 0;
    int height_ = 0;
};

} // namespace holobench::render::gl
