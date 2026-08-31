#pragma once

#include <array>
#include <complex>
#include <cstddef>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

#include "compute/fft/IFftBackend.hpp"

namespace holobench::compute::fft {

class OpenGlContextUnavailable final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class OpenGlFftError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

enum class TwiddleGenerationMode {
    GpuShader,
    CpuDeviceQuirk,
};

/**
 * Genuine OpenGL 4.6 compute-shader FFT backend using FP32 arithmetic for the
 * interactive path. ComplexField2D remains the FP64 reference representation;
 * non-zero values outside the normal FP32 domain are rejected because GPU
 * subnormal handling is not portable.
 *
 * The calling thread must own a current OpenGL 4.6 context for construction,
 * transforms, explicit resource release, and destruction after first use.
 * Call releaseGpuResources() before the owning context is destroyed.  No CPU
 * fallback is performed: unavailable contexts and GPU failures are explicit.
 */
class OpenGlComputeFftBackend final : public IFftBackend {
public:
    OpenGlComputeFftBackend();
    ~OpenGlComputeFftBackend() override;

    OpenGlComputeFftBackend(const OpenGlComputeFftBackend&) = delete;
    OpenGlComputeFftBackend& operator=(const OpenGlComputeFftBackend&) = delete;

    OpenGlComputeFftBackend(OpenGlComputeFftBackend&&) = delete;
    OpenGlComputeFftBackend& operator=(OpenGlComputeFftBackend&&) = delete;

    [[nodiscard]] std::string_view name() const noexcept override {
        return "OpenGL 4.6 compute-shader FFT (FP32 interactive)";
    }

    [[nodiscard]] bool supportsDimensions(std::size_t width, std::size_t height) const noexcept override;
    void forward2D(field::ComplexField2D& field) override;
    void inverse2D(field::ComplexField2D& field) override;

    // Executes forward FFT -> pointwise spectral multiply -> inverse FFT while
    // retaining intermediate data on the GPU. The transfer samples use native
    // unshifted FFT ordering and must match the field sample count.
    void applySpectralTransfer2D(
        field::ComplexField2D& field,
        std::span<const std::complex<double>> transferFunction);

    [[nodiscard]] static bool isContextAvailable() noexcept;
    [[nodiscard]] static bool requiresCpuTwiddleQuirk(
        std::string_view vendor,
        std::string_view renderer,
        std::string_view version) noexcept;
    [[nodiscard]] TwiddleGenerationMode twiddleGenerationMode() const noexcept {
        return twiddleGenerationMode_;
    }
    [[nodiscard]] static std::string_view twiddleGenerationModeName(
        TwiddleGenerationMode mode) noexcept;
    [[nodiscard]] std::size_t bufferCapacitySamples() const noexcept { return bufferCapacitySamples_; }
    [[nodiscard]] bool hasGpuResources() const noexcept {
        return programId_ != 0 || ssbos_[0] != 0 || ssbos_[1] != 0
            || ssbos_[2] != 0 || ssbos_[3] != 0;
    }

    // This is intentionally explicit as well as being called by the destructor.
    // If no compatible context is current, the call is a no-op so that a caller
    // can restore the owning context and retry without losing the resource names.
    void releaseGpuResources() noexcept;

private:
    struct alignas(8) GpuComplex final {
        float real = 0.0F;
        float imaginary = 0.0F;
    };
    static_assert(sizeof(GpuComplex) == 2U * sizeof(float));
    static_assert(alignof(GpuComplex) == 8U);

    void transform2D(field::ComplexField2D& field, bool inverse);
    void ensureResourcesInitialized(std::size_t sampleCount, std::size_t maximumDimension);
    void uploadTwiddles(std::size_t maximumDimension);
    [[nodiscard]] unsigned int executeDeviceTransform(
        std::size_t width,
        std::size_t height,
        std::size_t sampleCount,
        unsigned int inputBuffer,
        unsigned int outputBuffer,
        bool inverse);
    void validateGpuLimits(std::size_t width, std::size_t height, std::size_t sampleCount) const;

    unsigned int programId_ = 0;
    std::array<unsigned int, 4> ssbos_{0, 0, 0, 0};
    std::size_t bufferCapacitySamples_ = 0;
    std::size_t twiddleCapacitySamples_ = 0;
    std::size_t uploadedTwiddleDimension_ = 0;
    std::vector<GpuComplex> staging_;
    std::vector<GpuComplex> twiddleStaging_;
    std::vector<GpuComplex> spectralStaging_;

    int locPassType_ = -1;
    int locWidth_ = -1;
    int locHeight_ = -1;
    int locLog2Dim_ = -1;
    int locSubLength_ = -1;
    int locTwiddleStride_ = -1;
    int locTwiddleSign_ = -1;
    int locTwiddleDimension_ = -1;
    int locScale_ = -1;
    TwiddleGenerationMode twiddleGenerationMode_ = TwiddleGenerationMode::GpuShader;
    bool deviceProfileInitialized_ = false;
};

using GlComputeFftBackend = OpenGlComputeFftBackend;

} // namespace holobench::compute::fft
