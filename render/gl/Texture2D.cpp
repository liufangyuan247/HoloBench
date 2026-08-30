#include "render/gl/Texture2D.hpp"

#include <limits>
#include <utility>

namespace holobench::render::gl {

Texture2D::~Texture2D() {
    destroy();
}

Texture2D::Texture2D(Texture2D&& other) noexcept
    : textureId_(std::exchange(other.textureId_, 0))
    , width_(std::exchange(other.width_, 0))
    , height_(std::exchange(other.height_, 0)) {
}

Texture2D& Texture2D::operator=(Texture2D&& other) noexcept {
    if (this != &other) {
        destroy();
        textureId_ = std::exchange(other.textureId_, 0);
        width_ = std::exchange(other.width_, 0);
        height_ = std::exchange(other.height_, 0);
    }
    return *this;
}

void Texture2D::destroy() noexcept {
    if (textureId_ != 0) {
        glDeleteTextures(1, &textureId_);
        textureId_ = 0;
    }
    width_ = 0;
    height_ = 0;
}

bool Texture2D::uploadRgba8(int width, int height, std::span<const std::uint8_t> rgbaBytes) {
    if (width <= 0 || height <= 0) {
        return false;
    }
    const auto widthValue = static_cast<std::size_t>(width);
    const auto heightValue = static_cast<std::size_t>(height);
    if (widthValue > std::numeric_limits<std::size_t>::max() / heightValue) {
        return false;
    }
    const std::size_t pixelCount = widthValue * heightValue;
    if (pixelCount > std::numeric_limits<std::size_t>::max() / 4U) {
        return false;
    }
    const std::size_t expectedBytes = pixelCount * 4U;
    if (rgbaBytes.size() != expectedBytes) {
        return false;
    }

    if (textureId_ == 0) {
        glGenTextures(1, &textureId_);
        if (textureId_ == 0) {
            return false;
        }
    }

    GLint previousTexture = 0;
    GLint previousUnpackAlignment = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &previousUnpackAlignment);
    glBindTexture(GL_TEXTURE_2D, textureId_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    if (width != width_ || height != height_) {
        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            GL_RGBA8,
            width,
            height,
            0,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            rgbaBytes.data());
        width_ = width;
        height_ = height;
    } else {
        glTexSubImage2D(
            GL_TEXTURE_2D,
            0,
            0,
            0,
            width,
            height,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            rgbaBytes.data());
    }

    glPixelStorei(GL_UNPACK_ALIGNMENT, previousUnpackAlignment);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTexture));
    return true;
}

bool Texture2D::uploadImage(const field::RgbaImage& image) {
    constexpr auto maxInt = static_cast<std::size_t>(std::numeric_limits<int>::max());
    if (image.width() > maxInt || image.height() > maxInt) {
        return false;
    }
    return uploadRgba8(
        static_cast<int>(image.width()),
        static_cast<int>(image.height()),
        image.rgbaBytes());
}

void Texture2D::bind(GLuint unit) const noexcept {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, textureId_);
}

void Texture2D::unbind(GLuint unit) const noexcept {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, 0);
}

} // namespace holobench::render::gl
