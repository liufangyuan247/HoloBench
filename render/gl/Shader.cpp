#include "render/gl/Shader.hpp"

#include <SDL3/SDL.h>
#include <glm/gtc/type_ptr.hpp>

#include <limits>
#include <string>
#include <utility>

namespace holobench::render::gl {

namespace {

struct ShaderStageGuard {
    GLuint id = 0;

    explicit ShaderStageGuard(GLuint shaderId = 0) noexcept : id(shaderId) {}
    ~ShaderStageGuard() noexcept {
        if (id != 0) {
            glDeleteShader(id);
        }
    }

    ShaderStageGuard(const ShaderStageGuard&) = delete;
    ShaderStageGuard& operator=(const ShaderStageGuard&) = delete;

    ShaderStageGuard(ShaderStageGuard&& other) noexcept : id(std::exchange(other.id, 0)) {}
    ShaderStageGuard& operator=(ShaderStageGuard&& other) noexcept {
        if (this != &other) {
            if (id != 0) {
                glDeleteShader(id);
            }
            id = std::exchange(other.id, 0);
        }
        return *this;
    }

    [[nodiscard]] GLuint release() noexcept {
        return std::exchange(id, 0);
    }

    [[nodiscard]] GLuint get() const noexcept {
        return id;
    }
};

struct ProgramGuard {
    GLuint id = 0;

    explicit ProgramGuard(GLuint progId = 0) noexcept : id(progId) {}
    ~ProgramGuard() noexcept {
        if (id != 0) {
            glDeleteProgram(id);
        }
    }

    ProgramGuard(const ProgramGuard&) = delete;
    ProgramGuard& operator=(const ProgramGuard&) = delete;

    ProgramGuard(ProgramGuard&& other) noexcept : id(std::exchange(other.id, 0)) {}
    ProgramGuard& operator=(ProgramGuard&& other) noexcept {
        if (this != &other) {
            if (id != 0) {
                glDeleteProgram(id);
            }
            id = std::exchange(other.id, 0);
        }
        return *this;
    }

    [[nodiscard]] GLuint release() noexcept {
        return std::exchange(id, 0);
    }

    [[nodiscard]] GLuint get() const noexcept {
        return id;
    }
};

} // namespace

ShaderProgram::~ShaderProgram() {
    destroy();
}

ShaderProgram::ShaderProgram(ShaderProgram&& other) noexcept
    : programId_(std::exchange(other.programId_, 0)) {}

ShaderProgram& ShaderProgram::operator=(ShaderProgram&& other) noexcept {
    if (this != &other) {
        destroy();
        programId_ = std::exchange(other.programId_, 0);
    }
    return *this;
}

void ShaderProgram::destroy() noexcept {
    if (programId_ != 0) {
        glDeleteProgram(programId_);
        programId_ = 0;
    }
}

void ShaderProgram::use() const noexcept {
    if (programId_ != 0) {
        glUseProgram(programId_);
    }
}

GLint ShaderProgram::getUniformLocation(const char* name) const noexcept {
    if (programId_ == 0 || name == nullptr) {
        return -1;
    }
    return glGetUniformLocation(programId_, name);
}

void ShaderProgram::setMat4(const char* name, const glm::mat4& matrix) const noexcept {
    const GLint loc = getUniformLocation(name);
    if (loc >= 0) {
        glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(matrix));
    }
}

void ShaderProgram::setVec3(const char* name, const glm::vec3& vector) const noexcept {
    const GLint loc = getUniformLocation(name);
    if (loc >= 0) {
        glUniform3fv(loc, 1, glm::value_ptr(vector));
    }
}

void ShaderProgram::setVec4(const char* name, const glm::vec4& vector) const noexcept {
    const GLint loc = getUniformLocation(name);
    if (loc >= 0) {
        glUniform4fv(loc, 1, glm::value_ptr(vector));
    }
}

void ShaderProgram::setFloat(const char* name, float value) const noexcept {
    const GLint loc = getUniformLocation(name);
    if (loc >= 0) {
        glUniform1f(loc, value);
    }
}

void ShaderProgram::setInt(const char* name, int value) const noexcept {
    const GLint loc = getUniformLocation(name);
    if (loc >= 0) {
        glUniform1i(loc, value);
    }
}

GLuint ShaderProgram::compileStage(
    GLenum stageType,
    std::string_view source,
    std::string* outErrorMessage) {
    const char* stageName = (stageType == GL_VERTEX_SHADER)
        ? "Vertex"
        : ((stageType == GL_FRAGMENT_SHADER) ? "Fragment" : "Shader");

    if (source.size() > static_cast<std::size_t>(std::numeric_limits<GLint>::max())) {
        const std::string err = std::string(stageName) + " shader source size exceeds maximum supported length";
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "%s", err.c_str());
        if (outErrorMessage != nullptr) {
            *outErrorMessage = err;
        }
        return 0;
    }

    const GLuint shaderId = glCreateShader(stageType);
    if (shaderId == 0) {
        const std::string err = std::string("Failed to create OpenGL ") + stageName + " shader object";
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "%s", err.c_str());
        if (outErrorMessage != nullptr) {
            *outErrorMessage = err;
        }
        return 0;
    }

    ShaderStageGuard shaderGuard(shaderId);

    const auto* srcPtr = source.data();
    const auto srcLen = static_cast<GLint>(source.size());
    glShaderSource(shaderGuard.get(), 1, &srcPtr, &srcLen);
    glCompileShader(shaderGuard.get());

    GLint compileStatus = GL_FALSE;
    glGetShaderiv(shaderGuard.get(), GL_COMPILE_STATUS, &compileStatus);
    if (compileStatus != GL_TRUE) {
        GLint logLength = 0;
        glGetShaderiv(shaderGuard.get(), GL_INFO_LOG_LENGTH, &logLength);
        std::string infoLog;
        if (logLength > 1) {
            infoLog.resize(static_cast<std::size_t>(logLength));
            GLsizei writtenLength = 0;
            glGetShaderInfoLog(shaderGuard.get(), logLength, &writtenLength, infoLog.data());
            if (writtenLength >= 0 && static_cast<std::size_t>(writtenLength) < infoLog.size()) {
                infoLog.resize(static_cast<std::size_t>(writtenLength));
            }
        }
        while (!infoLog.empty() && (infoLog.back() == '\0' || infoLog.back() == '\r' || infoLog.back() == '\n')) {
            infoLog.pop_back();
        }
        if (infoLog.empty()) {
            infoLog = "(no info log provided by OpenGL driver)";
        }

        SDL_LogError(
            SDL_LOG_CATEGORY_RENDER,
            "%s shader compilation failed:\n%s",
            stageName,
            infoLog.c_str());

        if (outErrorMessage != nullptr) {
            *outErrorMessage = std::string(stageName) + " shader compile error: " + infoLog;
        }

        return 0;
    }

    return shaderGuard.release();
}

bool ShaderProgram::compileAndLink(
    std::string_view vertexSource,
    std::string_view fragmentSource,
    std::string* outErrorMessage) {
    if (outErrorMessage != nullptr) {
        outErrorMessage->clear();
    }

    destroy();

    const GLuint vertShaderId = compileStage(GL_VERTEX_SHADER, vertexSource, outErrorMessage);
    if (vertShaderId == 0) {
        return false;
    }
    ShaderStageGuard vertGuard(vertShaderId);

    const GLuint fragShaderId = compileStage(GL_FRAGMENT_SHADER, fragmentSource, outErrorMessage);
    if (fragShaderId == 0) {
        return false;
    }
    ShaderStageGuard fragGuard(fragShaderId);

    const GLuint progId = glCreateProgram();
    if (progId == 0) {
        const std::string err = "Failed to create OpenGL program object";
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "%s", err.c_str());
        if (outErrorMessage != nullptr) {
            *outErrorMessage = err;
        }
        return false;
    }
    ProgramGuard progGuard(progId);

    glAttachShader(progGuard.get(), vertGuard.get());
    glAttachShader(progGuard.get(), fragGuard.get());
    glLinkProgram(progGuard.get());

    GLint linkStatus = GL_FALSE;
    glGetProgramiv(progGuard.get(), GL_LINK_STATUS, &linkStatus);

    // Shaders can be detached once linked
    glDetachShader(progGuard.get(), vertGuard.get());
    glDetachShader(progGuard.get(), fragGuard.get());

    if (linkStatus != GL_TRUE) {
        GLint logLength = 0;
        glGetProgramiv(progGuard.get(), GL_INFO_LOG_LENGTH, &logLength);
        std::string infoLog;
        if (logLength > 1) {
            infoLog.resize(static_cast<std::size_t>(logLength));
            GLsizei writtenLength = 0;
            glGetProgramInfoLog(progGuard.get(), logLength, &writtenLength, infoLog.data());
            if (writtenLength >= 0 && static_cast<std::size_t>(writtenLength) < infoLog.size()) {
                infoLog.resize(static_cast<std::size_t>(writtenLength));
            }
        }
        while (!infoLog.empty() && (infoLog.back() == '\0' || infoLog.back() == '\r' || infoLog.back() == '\n')) {
            infoLog.pop_back();
        }
        if (infoLog.empty()) {
            infoLog = "(no info log provided by OpenGL driver)";
        }

        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Shader program linking failed:\n%s", infoLog.c_str());
        if (outErrorMessage != nullptr) {
            *outErrorMessage = "Shader link error: " + infoLog;
        }

        return false;
    }

    programId_ = progGuard.release();
    return true;
}

} // namespace holobench::render::gl
