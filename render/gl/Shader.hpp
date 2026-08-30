#pragma once

#include <glad/gl.h>
#include <glm/glm.hpp>

#include <string>
#include <string_view>

namespace holobench::render::gl {

class ShaderProgram final {
public:
    ShaderProgram() noexcept = default;
    ~ShaderProgram();

    ShaderProgram(const ShaderProgram&) = delete;
    ShaderProgram& operator=(const ShaderProgram&) = delete;

    ShaderProgram(ShaderProgram&& other) noexcept;
    ShaderProgram& operator=(ShaderProgram&& other) noexcept;

    [[nodiscard]] bool compileAndLink(
        std::string_view vertexSource,
        std::string_view fragmentSource,
        std::string* outErrorMessage = nullptr);

    void destroy() noexcept;

    void use() const noexcept;

    [[nodiscard]] bool isValid() const noexcept {
        return programId_ != 0;
    }

    [[nodiscard]] GLuint handle() const noexcept {
        return programId_;
    }

    [[nodiscard]] GLint getUniformLocation(const char* name) const noexcept;

    void setMat4(const char* name, const glm::mat4& matrix) const noexcept;
    void setVec3(const char* name, const glm::vec3& vector) const noexcept;
    void setVec4(const char* name, const glm::vec4& vector) const noexcept;
    void setFloat(const char* name, float value) const noexcept;
    void setInt(const char* name, int value) const noexcept;

private:
    [[nodiscard]] static GLuint compileStage(
        GLenum stageType,
        std::string_view source,
        std::string* outErrorMessage);

    GLuint programId_ = 0;
};

} // namespace holobench::render::gl
