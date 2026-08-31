#include "compute/fft/OpenGlComputeFftBackend.hpp"

#include <glad/gl.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <sstream>
#include <stdexcept>
#include <string>

#include "core/field/ComplexField2D.hpp"

namespace holobench::compute::fft {
namespace {

constexpr GLuint kLocalSize = 256U;

[[nodiscard]] constexpr bool isPowerOfTwo(std::size_t value) noexcept {
    return value != 0 && (value & (value - 1)) == 0;
}

void validateFiniteSamples(const field::ComplexField2D& field) {
    for (const auto& sample : field.samples()) {
        if (!std::isfinite(sample.real()) || !std::isfinite(sample.imag())) {
            throw std::invalid_argument("FFT input samples must be finite");
        }
        for (const double component : {sample.real(), sample.imag()}) {
            if (component != 0.0
                && std::abs(component) < static_cast<double>(std::numeric_limits<float>::min())) {
                throw std::underflow_error(
                    "OpenGL FP32 FFT input is below the normal float range");
            }
            const float converted = static_cast<float>(component);
            if (!std::isfinite(converted)) {
                throw std::overflow_error("OpenGL FP32 FFT input exceeds the finite float range");
            }
        }
    }
}

[[nodiscard]] const char* glErrorName(GLenum error) noexcept {
    switch (error) {
    case GL_INVALID_ENUM:
        return "GL_INVALID_ENUM";
    case GL_INVALID_VALUE:
        return "GL_INVALID_VALUE";
    case GL_INVALID_OPERATION:
        return "GL_INVALID_OPERATION";
    case GL_INVALID_FRAMEBUFFER_OPERATION:
        return "GL_INVALID_FRAMEBUFFER_OPERATION";
    case GL_OUT_OF_MEMORY:
        return "GL_OUT_OF_MEMORY";
    case GL_CONTEXT_LOST:
        return "GL_CONTEXT_LOST";
    default:
        return "unknown OpenGL error";
    }
}

void requireNoGlError(const char* operation) {
    const GLenum firstError = glGetError();
    if (firstError == GL_NO_ERROR) {
        return;
    }

    std::size_t additionalErrorCount = 0;
    while (glGetError() != GL_NO_ERROR) {
        ++additionalErrorCount;
    }

    std::ostringstream message;
    message << operation << " failed with " << glErrorName(firstError)
            << " (0x" << std::hex << firstError << std::dec << ')';
    if (additionalErrorCount != 0) {
        message << " and " << additionalErrorCount << " additional OpenGL error(s)";
    }
    throw OpenGlFftError(message.str());
}

class BindingStateGuard final {
public:
    BindingStateGuard() {
        glGetIntegerv(GL_CURRENT_PROGRAM, &program_);
        glGetIntegerv(GL_SHADER_STORAGE_BUFFER_BINDING, &genericSsbo_);
        glGetIntegeri_v(GL_SHADER_STORAGE_BUFFER_BINDING, 0U, &indexedSsbo0_);
        glGetIntegeri_v(GL_SHADER_STORAGE_BUFFER_BINDING, 1U, &indexedSsbo1_);
        glGetIntegeri_v(GL_SHADER_STORAGE_BUFFER_BINDING, 2U, &indexedSsbo2_);
        glGetIntegeri_v(GL_SHADER_STORAGE_BUFFER_BINDING, 3U, &indexedSsbo3_);
    }

    ~BindingStateGuard() {
        glUseProgram(static_cast<GLuint>(program_));
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0U, static_cast<GLuint>(indexedSsbo0_));
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1U, static_cast<GLuint>(indexedSsbo1_));
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2U, static_cast<GLuint>(indexedSsbo2_));
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3U, static_cast<GLuint>(indexedSsbo3_));
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, static_cast<GLuint>(genericSsbo_));
    }

    BindingStateGuard(const BindingStateGuard&) = delete;
    BindingStateGuard& operator=(const BindingStateGuard&) = delete;

private:
    GLint program_ = 0;
    GLint genericSsbo_ = 0;
    GLint indexedSsbo0_ = 0;
    GLint indexedSsbo1_ = 0;
    GLint indexedSsbo2_ = 0;
    GLint indexedSsbo3_ = 0;
};

constexpr const char* kComputeShaderSource = R"(#version 460 core

layout(local_size_x = 256, local_size_y = 1, local_size_z = 1) in;

struct ComplexValue {
    float real;
    float imaginary;
};

layout(std430, binding = 0) readonly buffer InputBuffer {
    ComplexValue inSamples[];
};

layout(std430, binding = 1) writeonly buffer OutputBuffer {
    ComplexValue outSamples[];
};

layout(std430, binding = 2) buffer TwiddleBuffer {
    ComplexValue twiddleSamples[];
};

layout(std430, binding = 3) readonly buffer SpectralTransferBuffer {
    ComplexValue spectralTransferSamples[];
};

uniform uint u_pass_type;
uniform uint u_width;
uniform uint u_height;
uniform uint u_log2_dim;
uniform uint u_sub_length;
uniform uint u_twiddle_stride;
uniform float u_twiddle_sign;
uniform uint u_twiddle_dimension;
uniform float u_scale;

uint reverseBits(uint x, uint bits) {
    if (bits == 0u) {
        return 0u;
    }
    return bitfieldReverse(x) >> (32u - bits);
}

vec2 loadComplex(uint index) {
    return vec2(inSamples[index].real, inSamples[index].imaginary);
}

void storeComplex(uint index, vec2 value) {
    outSamples[index].real = value.x;
    outSamples[index].imaginary = value.y;
}

void main() {
    uint gid = gl_GlobalInvocationID.x;

    if (u_pass_type == 0u) {
        // Bit reverse along rows
        if (gid >= u_width * u_height) {
            return;
        }
        uint y = gid / u_width;
        uint x = gid % u_width;
        uint x_rev = reverseBits(x, u_log2_dim);
        storeComplex(gid, loadComplex(y * u_width + x_rev));
    } else if (u_pass_type == 1u) {
        // Butterfly along rows
        uint totalButterflies = (u_width / 2u) * u_height;
        if (gid >= totalButterflies) {
            return;
        }
        uint butterfliesPerRow = u_width / 2u;
        uint y = gid / butterfliesPerRow;
        uint b = gid % butterfliesPerRow;
        uint halfLen = u_sub_length / 2u;
        uint group = b / halfLen;
        uint offset = b % halfLen;
        uint x0 = group * u_sub_length + offset;
        uint x1 = x0 + halfLen;
        uint idx0 = y * u_width + x0;
        uint idx1 = y * u_width + x1;
        uint twiddleIndex = offset * u_twiddle_stride;
        float twiddleReal = twiddleSamples[twiddleIndex].real;
        float twiddleImaginary = u_twiddle_sign * twiddleSamples[twiddleIndex].imaginary;
        float evenReal = inSamples[idx0].real;
        float evenImaginary = inSamples[idx0].imaginary;
        float inputOddReal = inSamples[idx1].real;
        float inputOddImaginary = inSamples[idx1].imaginary;
        float oddReal = inputOddReal * twiddleReal - inputOddImaginary * twiddleImaginary;
        float oddImaginary = inputOddReal * twiddleImaginary + inputOddImaginary * twiddleReal;
        outSamples[idx0].real = evenReal + oddReal;
        outSamples[idx0].imaginary = evenImaginary + oddImaginary;
        outSamples[idx1].real = evenReal - oddReal;
        outSamples[idx1].imaginary = evenImaginary - oddImaginary;
    } else if (u_pass_type == 2u) {
        // Bit reverse along columns
        if (gid >= u_width * u_height) {
            return;
        }
        uint y = gid / u_width;
        uint x = gid % u_width;
        uint y_rev = reverseBits(y, u_log2_dim);
        storeComplex(gid, loadComplex(y_rev * u_width + x));
    } else if (u_pass_type == 3u) {
        // Butterfly along columns
        uint totalButterflies = u_width * (u_height / 2u);
        if (gid >= totalButterflies) {
            return;
        }
        uint yButterfly = gid / u_width;
        uint x = gid % u_width;
        uint halfLen = u_sub_length / 2u;
        uint group = yButterfly / halfLen;
        uint offset = yButterfly % halfLen;
        uint y0 = group * u_sub_length + offset;
        uint y1 = y0 + halfLen;
        uint idx0 = y0 * u_width + x;
        uint idx1 = y1 * u_width + x;
        uint twiddleIndex = offset * u_twiddle_stride;
        float twiddleReal = twiddleSamples[twiddleIndex].real;
        float twiddleImaginary = u_twiddle_sign * twiddleSamples[twiddleIndex].imaginary;
        float evenReal = inSamples[idx0].real;
        float evenImaginary = inSamples[idx0].imaginary;
        float inputOddReal = inSamples[idx1].real;
        float inputOddImaginary = inSamples[idx1].imaginary;
        float oddReal = inputOddReal * twiddleReal - inputOddImaginary * twiddleImaginary;
        float oddImaginary = inputOddReal * twiddleImaginary + inputOddImaginary * twiddleReal;
        outSamples[idx0].real = evenReal + oddReal;
        outSamples[idx0].imaginary = evenImaginary + oddImaginary;
        outSamples[idx1].real = evenReal - oddReal;
        outSamples[idx1].imaginary = evenImaginary - oddImaginary;
    } else if (u_pass_type == 4u) {
        // Inverse FFT scaling
        if (gid >= u_width * u_height) {
            return;
        }
        storeComplex(gid, loadComplex(gid) * u_scale);
    } else if (u_pass_type == 5u) {
        // Pointwise complex spectral transfer.
        if (gid >= u_width * u_height) {
            return;
        }
        float inputReal = inSamples[gid].real;
        float inputImaginary = inSamples[gid].imaginary;
        float transferReal = spectralTransferSamples[gid].real;
        float transferImaginary = spectralTransferSamples[gid].imaginary;
        outSamples[gid].real = inputReal * transferReal - inputImaginary * transferImaginary;
        outSamples[gid].imaginary = inputReal * transferImaginary + inputImaginary * transferReal;
    } else if (u_pass_type == 6u) {
        // Default path: generate the maximum-dimension twiddle table on the GPU.
        uint twiddleCount = u_twiddle_dimension / 2u;
        if (gid >= twiddleCount) {
            return;
        }
        float angle = 6.28318530717958647692 * float(gid) / float(u_twiddle_dimension);
        twiddleSamples[gid].real = cos(angle);
        twiddleSamples[gid].imaginary = sin(angle);
    }
}
)";

} // namespace

OpenGlComputeFftBackend::OpenGlComputeFftBackend() = default;

OpenGlComputeFftBackend::~OpenGlComputeFftBackend() {
    releaseGpuResources();
}

bool OpenGlComputeFftBackend::isContextAvailable() noexcept {
    if (glGetString == nullptr || glGetIntegerv == nullptr || glGetIntegeri_v == nullptr
        || glGetInteger64v == nullptr || glGetError == nullptr || glUseProgram == nullptr
        || glBindBuffer == nullptr || glBindBufferBase == nullptr || glGenBuffers == nullptr
        || glDeleteBuffers == nullptr || glBufferData == nullptr || glBufferSubData == nullptr
        || glGetBufferSubData == nullptr || glCreateShader == nullptr || glShaderSource == nullptr
        || glCompileShader == nullptr || glGetShaderiv == nullptr || glGetShaderInfoLog == nullptr
        || glDeleteShader == nullptr || glCreateProgram == nullptr || glAttachShader == nullptr
        || glLinkProgram == nullptr || glDetachShader == nullptr || glGetProgramiv == nullptr
        || glGetProgramInfoLog == nullptr || glDeleteProgram == nullptr
        || glGetUniformLocation == nullptr || glUniform1ui == nullptr || glUniform1f == nullptr
        || glDispatchCompute == nullptr || glMemoryBarrier == nullptr) {
        return false;
    }
    const auto* versionStr = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    if (versionStr == nullptr) {
        return false;
    }
    GLint major = 0;
    GLint minor = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);
    if (major < 4 || (major == 4 && minor < 6)) {
        return false;
    }
    return true;
}

std::string_view OpenGlComputeFftBackend::twiddleGenerationModeName(
    TwiddleGenerationMode mode) noexcept {
    switch (mode) {
    case TwiddleGenerationMode::GpuShader:
        return "gpu-shader";
    case TwiddleGenerationMode::CpuValidationFallback:
        return "cpu-validation-fallback";
    }
    return "unknown";
}

bool OpenGlComputeFftBackend::supportsDimensions(std::size_t width, std::size_t height) const noexcept {
    return isPowerOfTwo(width) && isPowerOfTwo(height);
}

void OpenGlComputeFftBackend::validateGpuLimits(
    std::size_t width,
    std::size_t height,
    std::size_t sampleCount) const {
    constexpr auto maxGlUint = static_cast<std::size_t>(std::numeric_limits<GLuint>::max());
    if (width > maxGlUint || height > maxGlUint || sampleCount > maxGlUint) {
        throw std::invalid_argument("OpenGL compute FFT dimensions exceed 32-bit shader indexing");
    }
    if (sampleCount > std::numeric_limits<std::size_t>::max() / sizeof(GpuComplex)) {
        throw std::overflow_error("OpenGL compute FFT buffer byte count overflows size_t");
    }
    const std::size_t byteCount = sampleCount * sizeof(GpuComplex);
    if (byteCount > static_cast<std::size_t>(std::numeric_limits<GLsizeiptr>::max())) {
        throw std::overflow_error("OpenGL compute FFT buffer byte count exceeds GLsizeiptr");
    }

    GLint maximumInvocations = 0;
    GLint maximumLocalSizeX = 0;
    GLint maximumGroupCountX = 0;
    GLint maximumStorageBlocks = 0;
    GLint64 maximumStorageBlockBytes = 0;
    glGetIntegerv(GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS, &maximumInvocations);
    glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE, 0U, &maximumLocalSizeX);
    glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 0U, &maximumGroupCountX);
    glGetIntegerv(GL_MAX_COMPUTE_SHADER_STORAGE_BLOCKS, &maximumStorageBlocks);
    glGetInteger64v(GL_MAX_SHADER_STORAGE_BLOCK_SIZE, &maximumStorageBlockBytes);
    requireNoGlError("querying OpenGL compute limits");

    if (maximumInvocations < static_cast<GLint>(kLocalSize)
        || maximumLocalSizeX < static_cast<GLint>(kLocalSize)) {
        throw OpenGlFftError("OpenGL device cannot dispatch the FFT shader's 256-thread workgroup");
    }
    if (maximumStorageBlocks < 4) {
        throw OpenGlFftError("OpenGL compute stage exposes fewer than four shader-storage blocks");
    }
    if (maximumStorageBlockBytes <= 0
        || byteCount > static_cast<std::uint64_t>(maximumStorageBlockBytes)) {
        throw OpenGlFftError("FFT field exceeds GL_MAX_SHADER_STORAGE_BLOCK_SIZE");
    }
    const std::size_t groupCount = (sampleCount + kLocalSize - 1U) / kLocalSize;
    if (maximumGroupCountX <= 0
        || groupCount > static_cast<std::size_t>(maximumGroupCountX)) {
        throw OpenGlFftError("FFT dispatch exceeds GL_MAX_COMPUTE_WORK_GROUP_COUNT");
    }
}

void OpenGlComputeFftBackend::forward2D(field::ComplexField2D& field) {
    transform2D(field, false);
}

void OpenGlComputeFftBackend::inverse2D(field::ComplexField2D& field) {
    transform2D(field, true);
}

void OpenGlComputeFftBackend::applySpectralTransfer2D(
    field::ComplexField2D& field,
    std::span<const std::complex<double>> transferFunction) {
    const auto width = field.width();
    const auto height = field.height();
    const auto sampleCount = field.sampleCount();
    if (!supportsDimensions(width, height)) {
        throw std::invalid_argument("OpenGL spectral transfer requires power-of-two dimensions");
    }
    if (transferFunction.size() != sampleCount) {
        throw std::invalid_argument("OpenGL spectral transfer sample count does not match the field");
    }
    validateFiniteSamples(field);
    for (const auto& sample : transferFunction) {
        if (!std::isfinite(sample.real()) || !std::isfinite(sample.imag())) {
            throw std::invalid_argument("OpenGL spectral transfer samples must be finite");
        }
        for (const double component : {sample.real(), sample.imag()}) {
            if (component != 0.0
                && std::abs(component) < static_cast<double>(std::numeric_limits<float>::min())) {
                throw std::underflow_error(
                    "OpenGL spectral transfer is below the normal float range");
            }
            const float converted = static_cast<float>(component);
            if (!std::isfinite(converted)) {
                throw std::overflow_error("OpenGL spectral transfer exceeds the finite float range");
            }
        }
    }
    if (!isContextAvailable()) {
        throw OpenGlContextUnavailable(
            "OpenGL spectral transfer requires an active OpenGL 4.6 context on the calling thread");
    }

    requireNoGlError("entering OpenGL fused spectral transfer");
    BindingStateGuard bindingState;
    requireNoGlError("saving OpenGL binding state");
    validateGpuLimits(width, height, sampleCount);
    ensureResourcesInitialized(sampleCount, std::max(width, height));
    uploadTwiddles(std::max(width, height));

    staging_.resize(sampleCount);
    spectralStaging_.resize(sampleCount);
    for (std::size_t index = 0; index < sampleCount; ++index) {
        staging_[index] = GpuComplex {
            static_cast<float>(field.samples()[index].real()),
            static_cast<float>(field.samples()[index].imag())};
        spectralStaging_[index] = GpuComplex {
            static_cast<float>(transferFunction[index].real()),
            static_cast<float>(transferFunction[index].imag())};
    }

    glUseProgram(programId_);
    const GLsizeiptr byteCount = static_cast<GLsizeiptr>(sampleCount * sizeof(GpuComplex));
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbos_[0]);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, byteCount, staging_.data());
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbos_[3]);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, byteCount, spectralStaging_.data());
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    requireNoGlError("uploading fused FFT input and spectral transfer");

    const GLuint forwardBuffer = executeDeviceTransform(
        width,
        height,
        sampleCount,
        ssbos_[0],
        ssbos_[1],
        false);
    const GLuint filteredBuffer = forwardBuffer == ssbos_[0] ? ssbos_[1] : ssbos_[0];
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0U, forwardBuffer);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1U, filteredBuffer);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3U, ssbos_[3]);
    glUniform1ui(locPassType_, 5U);
    const GLuint groupCount = static_cast<GLuint>((sampleCount + kLocalSize - 1U) / kLocalSize);
    glDispatchCompute(groupCount, 1, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
    requireNoGlError("dispatching pointwise spectral transfer");

    const GLuint resultBuffer = executeDeviceTransform(
        width,
        height,
        sampleCount,
        filteredBuffer,
        forwardBuffer,
        true);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, resultBuffer);
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, byteCount, staging_.data());
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    requireNoGlError("reading fused spectral-transfer output field");

    for (const auto& sample : staging_) {
        if (!std::isfinite(sample.real) || !std::isfinite(sample.imaginary)) {
            throw OpenGlFftError("OpenGL fused spectral transfer produced a non-finite sample");
        }
    }
    for (std::size_t index = 0; index < sampleCount; ++index) {
        field.samples()[index] = {staging_[index].real, staging_[index].imaginary};
    }
}

void OpenGlComputeFftBackend::releaseGpuResources() noexcept {
    if (hasGpuResources() && !isContextAvailable()) {
        return;
    }
    if (ssbos_[0] != 0 || ssbos_[1] != 0 || ssbos_[2] != 0 || ssbos_[3] != 0) {
        glDeleteBuffers(4, ssbos_.data());
        ssbos_ = {0, 0, 0, 0};
    }
    if (programId_ != 0) {
        glDeleteProgram(programId_);
        programId_ = 0;
    }
    bufferCapacitySamples_ = 0;
    twiddleCapacitySamples_ = 0;
    uploadedTwiddleDimension_ = 0;
    staging_.clear();
    twiddleStaging_.clear();
    spectralStaging_.clear();
    locPassType_ = -1;
    locWidth_ = -1;
    locHeight_ = -1;
    locLog2Dim_ = -1;
    locSubLength_ = -1;
    locTwiddleStride_ = -1;
    locTwiddleSign_ = -1;
    locTwiddleDimension_ = -1;
    locScale_ = -1;
    twiddleGenerationMode_ = TwiddleGenerationMode::GpuShader;
}

void OpenGlComputeFftBackend::ensureResourcesInitialized(
    std::size_t sampleCount,
    std::size_t maximumDimension) {
    if (programId_ == 0) {
        const GLuint shaderId = glCreateShader(GL_COMPUTE_SHADER);
        if (shaderId == 0) {
            throw OpenGlFftError("Failed to create compute shader object for FFT backend");
        }

        const auto* srcPtr = kComputeShaderSource;
        glShaderSource(shaderId, 1, &srcPtr, nullptr);
        glCompileShader(shaderId);

        GLint compileStatus = GL_FALSE;
        glGetShaderiv(shaderId, GL_COMPILE_STATUS, &compileStatus);
        if (compileStatus != GL_TRUE) {
            GLint logLength = 0;
            glGetShaderiv(shaderId, GL_INFO_LOG_LENGTH, &logLength);
            std::string log;
            if (logLength > 1) {
                log.resize(static_cast<std::size_t>(logLength));
                GLsizei written = 0;
                glGetShaderInfoLog(shaderId, logLength, &written, log.data());
                if (written >= 0 && static_cast<std::size_t>(written) < log.size()) {
                    log.resize(static_cast<std::size_t>(written));
                }
            }
            glDeleteShader(shaderId);
            throw OpenGlFftError("FFT compute shader compilation failed: " + log);
        }

        const GLuint progId = glCreateProgram();
        if (progId == 0) {
            glDeleteShader(shaderId);
            throw OpenGlFftError("Failed to create compute shader program for FFT backend");
        }

        glAttachShader(progId, shaderId);
        glLinkProgram(progId);
        glDetachShader(progId, shaderId);
        glDeleteShader(shaderId);

        GLint linkStatus = GL_FALSE;
        glGetProgramiv(progId, GL_LINK_STATUS, &linkStatus);
        if (linkStatus != GL_TRUE) {
            GLint logLength = 0;
            glGetProgramiv(progId, GL_INFO_LOG_LENGTH, &logLength);
            std::string log;
            if (logLength > 1) {
                log.resize(static_cast<std::size_t>(logLength));
                GLsizei written = 0;
                glGetProgramInfoLog(progId, logLength, &written, log.data());
                if (written >= 0 && static_cast<std::size_t>(written) < log.size()) {
                    log.resize(static_cast<std::size_t>(written));
                }
            }
            glDeleteProgram(progId);
            throw OpenGlFftError("FFT compute shader program link failed: " + log);
        }

        const GLint passTypeLocation = glGetUniformLocation(progId, "u_pass_type");
        const GLint widthLocation = glGetUniformLocation(progId, "u_width");
        const GLint heightLocation = glGetUniformLocation(progId, "u_height");
        const GLint log2DimLocation = glGetUniformLocation(progId, "u_log2_dim");
        const GLint subLengthLocation = glGetUniformLocation(progId, "u_sub_length");
        const GLint twiddleStrideLocation = glGetUniformLocation(progId, "u_twiddle_stride");
        const GLint twiddleSignLocation = glGetUniformLocation(progId, "u_twiddle_sign");
        const GLint twiddleDimensionLocation = glGetUniformLocation(progId, "u_twiddle_dimension");
        const GLint scaleLocation = glGetUniformLocation(progId, "u_scale");
        if (passTypeLocation < 0 || widthLocation < 0 || heightLocation < 0
            || log2DimLocation < 0 || subLengthLocation < 0
            || twiddleStrideLocation < 0 || twiddleSignLocation < 0
            || twiddleDimensionLocation < 0 || scaleLocation < 0) {
            glDeleteProgram(progId);
            throw OpenGlFftError("FFT compute shader is missing a required active uniform");
        }
        try {
            requireNoGlError("creating the FFT compute program");
        } catch (...) {
            glDeleteProgram(progId);
            throw;
        }
        programId_ = progId;
        locPassType_ = passTypeLocation;
        locWidth_ = widthLocation;
        locHeight_ = heightLocation;
        locLog2Dim_ = log2DimLocation;
        locSubLength_ = subLengthLocation;
        locTwiddleStride_ = twiddleStrideLocation;
        locTwiddleSign_ = twiddleSignLocation;
        locTwiddleDimension_ = twiddleDimensionLocation;
        locScale_ = scaleLocation;
    }

    if (sampleCount > bufferCapacitySamples_) {
        staging_.reserve(sampleCount);
        spectralStaging_.reserve(sampleCount);
        std::array<GLuint, 3> newSsbos {0, 0, 0};
        glGenBuffers(static_cast<GLsizei>(newSsbos.size()), newSsbos.data());
        if (newSsbos[0] == 0 || newSsbos[1] == 0 || newSsbos[2] == 0) {
            if (newSsbos[0] != 0 || newSsbos[1] != 0 || newSsbos[2] != 0) {
                glDeleteBuffers(static_cast<GLsizei>(newSsbos.size()), newSsbos.data());
            }
            throw OpenGlFftError("OpenGL failed to allocate FFT shader-storage buffer names");
        }
        const GLsizeiptr bufferBytes = static_cast<GLsizeiptr>(sampleCount * sizeof(GpuComplex));
        try {
            for (std::size_t i = 0; i < newSsbos.size(); ++i) {
                glBindBuffer(GL_SHADER_STORAGE_BUFFER, newSsbos[i]);
                glBufferData(GL_SHADER_STORAGE_BUFFER, bufferBytes, nullptr, GL_DYNAMIC_COPY);
                requireNoGlError("allocating FFT shader-storage buffers");
            }
        } catch (...) {
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
            glDeleteBuffers(static_cast<GLsizei>(newSsbos.size()), newSsbos.data());
            throw;
        }
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        std::array<GLuint, 3> oldSsbos {ssbos_[0], ssbos_[1], ssbos_[3]};
        if (oldSsbos[0] != 0 || oldSsbos[1] != 0 || oldSsbos[2] != 0) {
            glDeleteBuffers(static_cast<GLsizei>(oldSsbos.size()), oldSsbos.data());
        }
        ssbos_[0] = newSsbos[0];
        ssbos_[1] = newSsbos[1];
        ssbos_[3] = newSsbos[2];
        bufferCapacitySamples_ = sampleCount;
    }

    const std::size_t requiredTwiddleCount = maximumDimension > 1 ? maximumDimension / 2 : 0;
    if (requiredTwiddleCount > twiddleCapacitySamples_) {
        twiddleStaging_.reserve(requiredTwiddleCount);
        GLuint newTwiddleSsbo = 0;
        glGenBuffers(1, &newTwiddleSsbo);
        if (newTwiddleSsbo == 0) {
            throw OpenGlFftError("OpenGL failed to allocate the FFT twiddle buffer name");
        }
        const GLsizeiptr twiddleBytes = static_cast<GLsizeiptr>(
            requiredTwiddleCount * sizeof(GpuComplex));
        try {
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, newTwiddleSsbo);
            glBufferData(GL_SHADER_STORAGE_BUFFER, twiddleBytes, nullptr, GL_DYNAMIC_COPY);
            requireNoGlError("allocating the FFT twiddle buffer");
        } catch (...) {
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
            glDeleteBuffers(1, &newTwiddleSsbo);
            throw;
        }
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        if (ssbos_[2] != 0) {
            glDeleteBuffers(1, &ssbos_[2]);
        }
        ssbos_[2] = newTwiddleSsbo;
        twiddleCapacitySamples_ = requiredTwiddleCount;
        uploadedTwiddleDimension_ = 0;
    }
}

void OpenGlComputeFftBackend::uploadTwiddles(std::size_t maximumDimension) {
    if (maximumDimension <= 1) {
        return;
    }
    if (uploadedTwiddleDimension_ == maximumDimension) {
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2U, ssbos_[2]);
        requireNoGlError("binding cached FFT twiddle factors");
        return;
    }
    const std::size_t count = maximumDimension / 2;
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2U, ssbos_[2]);
    const auto uploadCpuTwiddles = [&]() {
        twiddleStaging_.resize(count);
        for (std::size_t index = 0; index < count; ++index) {
            const double angle = 2.0 * std::numbers::pi
                * static_cast<double>(index) / static_cast<double>(maximumDimension);
            twiddleStaging_[index] = GpuComplex {
                static_cast<float>(std::cos(angle)),
                static_cast<float>(std::sin(angle))};
        }
        const GLsizeiptr byteCount = static_cast<GLsizeiptr>(count * sizeof(GpuComplex));
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbos_[2]);
        glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, byteCount, twiddleStaging_.data());
        requireNoGlError("uploading validated CPU FFT twiddle factors");
    };
    if (twiddleGenerationMode_
        == TwiddleGenerationMode::CpuValidationFallback) {
        uploadCpuTwiddles();
    } else {
        glUseProgram(programId_);
        glUniform1ui(locPassType_, 6U);
        glUniform1ui(locTwiddleDimension_, static_cast<GLuint>(maximumDimension));
        const GLuint groupCount = static_cast<GLuint>((count + kLocalSize - 1U) / kLocalSize);
        glDispatchCompute(groupCount, 1, 1);
        glMemoryBarrier(
            GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
        requireNoGlError("generating FFT twiddle factors on the GPU");

        twiddleStaging_.resize(count);
        const GLsizeiptr byteCount = static_cast<GLsizeiptr>(
            count * sizeof(GpuComplex));
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbos_[2]);
        glGetBufferSubData(
            GL_SHADER_STORAGE_BUFFER, 0, byteCount, twiddleStaging_.data());
        requireNoGlError("validating GPU-generated FFT twiddle factors");
        constexpr float validationTolerance = 2.0e-5F;
        bool accurate = true;
        for (std::size_t index = 0; index < count; ++index) {
            const double angle = 2.0 * std::numbers::pi
                * static_cast<double>(index)
                / static_cast<double>(maximumDimension);
            const float expectedReal = static_cast<float>(std::cos(angle));
            const float expectedImaginary = static_cast<float>(std::sin(angle));
            accurate = accurate
                && std::isfinite(twiddleStaging_[index].real)
                && std::isfinite(twiddleStaging_[index].imaginary)
                && std::abs(twiddleStaging_[index].real - expectedReal)
                    <= validationTolerance
                && std::abs(
                    twiddleStaging_[index].imaginary - expectedImaginary)
                    <= validationTolerance;
        }
        if (!accurate) {
            twiddleGenerationMode_
                = TwiddleGenerationMode::CpuValidationFallback;
            uploadCpuTwiddles();
        }
    }
    uploadedTwiddleDimension_ = maximumDimension;
}

GLuint OpenGlComputeFftBackend::executeDeviceTransform(
    std::size_t width,
    std::size_t height,
    std::size_t sampleCount,
    GLuint inputBuffer,
    GLuint outputBuffer,
    bool inverse) {
    glUniform1ui(locWidth_, static_cast<GLuint>(width));
    glUniform1ui(locHeight_, static_cast<GLuint>(height));
    glUniform1f(locTwiddleSign_, inverse ? 1.0F : -1.0F);
    requireNoGlError("setting FFT transform uniforms");

    const std::size_t twiddleDimension = std::max(width, height);
    GLuint inBuffer = inputBuffer;
    GLuint outBuffer = outputBuffer;
    if (width > 1) {
        const auto log2Width = static_cast<GLuint>(std::countr_zero(width));
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, inBuffer);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, outBuffer);
        glUniform1ui(locPassType_, 0u);
        glUniform1ui(locLog2Dim_, log2Width);
        const GLuint numGroups = static_cast<GLuint>((sampleCount + kLocalSize - 1) / kLocalSize);
        glDispatchCompute(numGroups, 1, 1);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        requireNoGlError("dispatching FFT row bit reversal");
        std::swap(inBuffer, outBuffer);

        const std::size_t totalButterflies = (width / 2) * height;
        const GLuint butterflyGroups = static_cast<GLuint>(
            (totalButterflies + kLocalSize - 1) / kLocalSize);
        glUniform1ui(locPassType_, 1u);
        for (std::size_t length = 2;; length *= 2) {
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, inBuffer);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, outBuffer);
            glUniform1ui(locSubLength_, static_cast<GLuint>(length));
            glUniform1ui(locTwiddleStride_, static_cast<GLuint>(twiddleDimension / length));
            glDispatchCompute(butterflyGroups, 1, 1);
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
            requireNoGlError("dispatching FFT row butterfly stage");
            std::swap(inBuffer, outBuffer);
            if (length == width) {
                break;
            }
        }
    }

    if (height > 1) {
        const auto log2Height = static_cast<GLuint>(std::countr_zero(height));
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, inBuffer);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, outBuffer);
        glUniform1ui(locPassType_, 2u);
        glUniform1ui(locLog2Dim_, log2Height);
        const GLuint numGroups = static_cast<GLuint>((sampleCount + kLocalSize - 1) / kLocalSize);
        glDispatchCompute(numGroups, 1, 1);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        requireNoGlError("dispatching FFT column bit reversal");
        std::swap(inBuffer, outBuffer);

        const std::size_t totalButterflies = width * (height / 2);
        const GLuint butterflyGroups = static_cast<GLuint>(
            (totalButterflies + kLocalSize - 1) / kLocalSize);
        glUniform1ui(locPassType_, 3u);
        for (std::size_t length = 2;; length *= 2) {
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, inBuffer);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, outBuffer);
            glUniform1ui(locSubLength_, static_cast<GLuint>(length));
            glUniform1ui(locTwiddleStride_, static_cast<GLuint>(twiddleDimension / length));
            glDispatchCompute(butterflyGroups, 1, 1);
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
            requireNoGlError("dispatching FFT column butterfly stage");
            std::swap(inBuffer, outBuffer);
            if (length == height) {
                break;
            }
        }
    }

    if (inverse) {
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, inBuffer);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, outBuffer);
        glUniform1ui(locPassType_, 4u);
        const float scale = 1.0F / static_cast<float>(sampleCount);
        glUniform1f(locScale_, scale);
        const GLuint numGroups = static_cast<GLuint>((sampleCount + kLocalSize - 1) / kLocalSize);
        glDispatchCompute(numGroups, 1, 1);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        requireNoGlError("dispatching inverse FFT normalization");
        std::swap(inBuffer, outBuffer);
    }
    return inBuffer;
}

void OpenGlComputeFftBackend::transform2D(field::ComplexField2D& field, bool inverse) {
    const auto width = field.width();
    const auto height = field.height();
    if (!supportsDimensions(width, height)) {
        throw std::invalid_argument("OpenGL compute FFT requires power-of-two width and height");
    }
    validateFiniteSamples(field);

    if (!isContextAvailable()) {
        throw OpenGlContextUnavailable(
            "OpenGL compute FFT backend requires an active OpenGL 4.6 context on the calling thread");
    }

    const auto sampleCount = field.sampleCount();
    requireNoGlError("entering OpenGL compute FFT");
    BindingStateGuard bindingState;
    requireNoGlError("saving OpenGL binding state");
    validateGpuLimits(width, height, sampleCount);
    ensureResourcesInitialized(sampleCount, std::max(width, height));
    uploadTwiddles(std::max(width, height));

    staging_.resize(sampleCount);
    for (std::size_t index = 0; index < sampleCount; ++index) {
        staging_[index] = GpuComplex {
            static_cast<float>(field.samples()[index].real()),
            static_cast<float>(field.samples()[index].imag())};
    }

    glUseProgram(programId_);

    const GLsizeiptr byteCount = static_cast<GLsizeiptr>(sampleCount * sizeof(GpuComplex));
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbos_[0]);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, byteCount, staging_.data());
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    requireNoGlError("uploading FFT input field");

    const GLuint resultBuffer = executeDeviceTransform(
        width,
        height,
        sampleCount,
        ssbos_[0],
        ssbos_[1],
        inverse);

    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, resultBuffer);
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, byteCount, staging_.data());
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    requireNoGlError("reading FFT output field");

    for (const auto& sample : staging_) {
        if (!std::isfinite(sample.real) || !std::isfinite(sample.imaginary)) {
            throw OpenGlFftError("OpenGL compute FFT produced a non-finite sample");
        }
    }
    for (std::size_t index = 0; index < sampleCount; ++index) {
        field.samples()[index] = {staging_[index].real, staging_[index].imaginary};
    }
}

} // namespace holobench::compute::fft
