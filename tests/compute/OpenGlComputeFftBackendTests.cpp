#include <glad/gl.h>
#include <SDL3/SDL.h>
#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdio>
#include <limits>
#include <string_view>
#include <vector>

#include "compute/fft/CpuFftBackend.hpp"
#include "compute/fft/OpenGlComputeFftBackend.hpp"
#include "compute/fourier/FourFSystem.hpp"
#include "compute/propagation/AngularSpectrumPropagator.hpp"
#include "compute/propagation/OpenGlAngularSpectrumPropagator.hpp"
#include "compute/propagation/FresnelPropagator.hpp"
#include "core/field/ComplexField2D.hpp"

namespace fft = holobench::compute::fft;
namespace fourier = holobench::compute::fourier;
namespace propagation = holobench::compute::propagation;
namespace field = holobench::field;

namespace {

SDL_Window* gpuTestWindow = nullptr;
SDL_GLContext gpuTestContext = nullptr;

GLADapiproc loadOpenGlProcedure(const char* name) {
    return reinterpret_cast<GLADapiproc>(SDL_GL_GetProcAddress(name));
}

field::ComplexField2D makeField(std::size_t width, std::size_t height) {
    return field::ComplexField2D(width, height, 3.2e-6, 5.1e-6, 532e-9);
}

void fillDeterministic(field::ComplexField2D& value) {
    for (std::size_t index = 0; index < value.sampleCount(); ++index) {
        const double coordinate = static_cast<double>(index);
        value.samples()[index] = {
            std::sin(coordinate * 0.173) + static_cast<double>(index % 5U) * 0.125,
            std::cos(coordinate * 0.097) - static_cast<double>(index % 7U) * 0.0625};
    }
}

void checkFieldsNear(
    const field::ComplexField2D& actual,
    const field::ComplexField2D& expected,
    double relativeTolerance) {
    REQUIRE(actual.width() == expected.width());
    REQUIRE(actual.height() == expected.height());
    REQUIRE(actual.sampleCount() == expected.sampleCount());
    for (std::size_t index = 0; index < actual.sampleCount(); ++index) {
        CAPTURE(index);
        CAPTURE(actual.samples()[index].real());
        CAPTURE(actual.samples()[index].imag());
        CAPTURE(expected.samples()[index].real());
        CAPTURE(expected.samples()[index].imag());
        const double scale = std::max({
            1.0,
            std::abs(actual.samples()[index]),
            std::abs(expected.samples()[index])});
        CHECK(std::abs(actual.samples()[index] - expected.samples()[index])
            <= relativeTolerance * scale);
    }
}

bool fieldsExactlyEqual(
    const field::ComplexField2D& first,
    const field::ComplexField2D& second) {
    return first.width() == second.width()
        && first.height() == second.height()
        && std::equal(
            first.samples().begin(),
            first.samples().end(),
            second.samples().begin(),
            second.samples().end());
}

class ScopedNoCurrentContext final {
public:
    ScopedNoCurrentContext()
        : unbound_(SDL_GL_MakeCurrent(gpuTestWindow, nullptr)) {
    }

    ~ScopedNoCurrentContext() {
        if (unbound_) {
            static_cast<void>(SDL_GL_MakeCurrent(gpuTestWindow, gpuTestContext));
        }
    }

    [[nodiscard]] bool unbound() const noexcept { return unbound_; }

    ScopedNoCurrentContext(const ScopedNoCurrentContext&) = delete;
    ScopedNoCurrentContext& operator=(const ScopedNoCurrentContext&) = delete;

private:
    bool unbound_ = false;
};

bool initializeGpuTestContext() {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "GPU tests skipped: SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    static_cast<void>(SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4));
    static_cast<void>(SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 6));
    static_cast<void>(SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE));
    static_cast<void>(SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG));

    gpuTestWindow = SDL_CreateWindow(
        "HoloBench OpenGL compute tests",
        64,
        64,
        SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (gpuTestWindow == nullptr) {
        std::fprintf(stderr, "GPU tests skipped: SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return false;
    }

    gpuTestContext = SDL_GL_CreateContext(gpuTestWindow);
    if (gpuTestContext == nullptr || !SDL_GL_MakeCurrent(gpuTestWindow, gpuTestContext)) {
        std::fprintf(stderr, "GPU tests skipped: OpenGL context creation failed: %s\n", SDL_GetError());
        if (gpuTestContext != nullptr) {
            SDL_GL_DestroyContext(gpuTestContext);
            gpuTestContext = nullptr;
        }
        SDL_DestroyWindow(gpuTestWindow);
        gpuTestWindow = nullptr;
        SDL_Quit();
        return false;
    }

    const int loadedVersion = gladLoadGL(loadOpenGlProcedure);
    if (loadedVersion == 0
        || GLAD_VERSION_MAJOR(loadedVersion) < 4
        || (GLAD_VERSION_MAJOR(loadedVersion) == 4 && GLAD_VERSION_MINOR(loadedVersion) < 6)) {
        std::fprintf(stderr, "GPU tests skipped: OpenGL 4.6 Core is unavailable\n");
        SDL_GL_DestroyContext(gpuTestContext);
        gpuTestContext = nullptr;
        SDL_DestroyWindow(gpuTestWindow);
        gpuTestWindow = nullptr;
        SDL_Quit();
        return false;
    }

    const auto* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    const auto* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    std::fprintf(
        stderr,
        "GPU tests: renderer=%s, version=%s\n",
        renderer != nullptr ? renderer : "unknown",
        version != nullptr ? version : "unknown");
    return true;
}

void shutdownGpuTestContext() noexcept {
    if (gpuTestContext != nullptr) {
        static_cast<void>(SDL_GL_MakeCurrent(gpuTestWindow, gpuTestContext));
        glFinish();
        SDL_GL_DestroyContext(gpuTestContext);
        gpuTestContext = nullptr;
    }
    if (gpuTestWindow != nullptr) {
        SDL_DestroyWindow(gpuTestWindow);
        gpuTestWindow = nullptr;
    }
    SDL_Quit();
}

} // namespace

TEST_SUITE("OpenGL compute FFT") {

TEST_CASE("backend reports its contract and supported dimensions") {
    fft::OpenGlComputeFftBackend backend;
    CHECK(backend.name() == std::string_view("OpenGL 4.6 compute-shader FFT (FP32 interactive)"));
    CHECK(backend.supportsDimensions(1, 1));
    CHECK(backend.supportsDimensions(16, 8));
    CHECK_FALSE(backend.supportsDimensions(0, 8));
    CHECK_FALSE(backend.supportsDimensions(12, 8));
    CHECK(fft::OpenGlComputeFftBackend::isContextAvailable());
    CHECK_FALSE(backend.hasGpuResources());
    CHECK(fft::OpenGlComputeFftBackend::requiresCpuTwiddleQuirk(
        "ATI Technologies Inc.",
        "AMD Radeon Pro 5300M",
        "4.6.0 Core Profile Context 23.9.3.230915"));
    CHECK_FALSE(fft::OpenGlComputeFftBackend::requiresCpuTwiddleQuirk(
        "ATI Technologies Inc.",
        "AMD Radeon Pro 5300M",
        "4.6.0 Core Profile Context 24.1.0"));
    CHECK_FALSE(fft::OpenGlComputeFftBackend::requiresCpuTwiddleQuirk(
        "NVIDIA Corporation",
        "NVIDIA GeForce RTX 4090",
        "4.6.0 NVIDIA 580.0"));
}

TEST_CASE("missing current context is distinct and preserves the caller field") {
    fft::OpenGlComputeFftBackend backend;
    auto value = makeField(8, 4);
    fillDeterministic(value);
    const auto before = value;

    {
        ScopedNoCurrentContext noContext;
        REQUIRE(noContext.unbound());
        CHECK_FALSE(fft::OpenGlComputeFftBackend::isContextAvailable());
        CHECK_THROWS_AS(backend.forward2D(value), fft::OpenGlContextUnavailable);
    }

    REQUIRE(fft::OpenGlComputeFftBackend::isContextAvailable());
    CHECK(fieldsExactlyEqual(value, before));
    CHECK_FALSE(backend.hasGpuResources());
}

TEST_CASE("forward inverse and rectangular transforms agree with the CPU reference") {
    constexpr double tolerance = 3e-6;
    for (const auto dimensions : {
             std::pair<std::size_t, std::size_t> {8, 4},
             std::pair<std::size_t, std::size_t> {16, 8},
             std::pair<std::size_t, std::size_t> {1, 8},
             std::pair<std::size_t, std::size_t> {8, 1},
             std::pair<std::size_t, std::size_t> {1, 1}}) {
        auto actual = makeField(dimensions.first, dimensions.second);
        fillDeterministic(actual);
        const auto original = actual;
        auto expected = actual;

        fft::CpuFftBackend cpu;
        cpu.forward2D(expected);

        fft::OpenGlComputeFftBackend gpu;
        gpu.forward2D(actual);
        const auto* vendor = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
        const auto* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
        const auto* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
        const bool expectedCpuQuirk = fft::OpenGlComputeFftBackend::requiresCpuTwiddleQuirk(
            vendor != nullptr ? std::string_view(vendor) : std::string_view {},
            renderer != nullptr ? std::string_view(renderer) : std::string_view {},
            version != nullptr ? std::string_view(version) : std::string_view {});
        CHECK(gpu.twiddleGenerationMode()
            == (expectedCpuQuirk
                    ? fft::TwiddleGenerationMode::CpuDeviceQuirk
                    : fft::TwiddleGenerationMode::GpuShader));
        checkFieldsNear(actual, expected, tolerance);

        gpu.inverse2D(actual);
        checkFieldsNear(actual, original, tolerance);
        gpu.releaseGpuResources();
        CHECK_FALSE(gpu.hasGpuResources());
        CHECK(glGetError() == GL_NO_ERROR);
    }
}

TEST_CASE("resource capacity is reused and grows without changing numerical results") {
    fft::OpenGlComputeFftBackend backend;
    auto small = makeField(8, 4);
    fillDeterministic(small);
    backend.forward2D(small);
    const auto firstCapacity = backend.bufferCapacitySamples();
    CHECK(firstCapacity >= small.sampleCount());

    backend.inverse2D(small);
    CHECK(backend.bufferCapacitySamples() == firstCapacity);

    auto larger = makeField(16, 8);
    fillDeterministic(larger);
    backend.forward2D(larger);
    CHECK(backend.bufferCapacitySamples() >= larger.sampleCount());
    CHECK(backend.bufferCapacitySamples() > firstCapacity);

    backend.releaseGpuResources();
    CHECK(backend.bufferCapacitySamples() == 0);
    CHECK_FALSE(backend.hasGpuResources());
    CHECK(glGetError() == GL_NO_ERROR);
}

TEST_CASE("ASM and Fresnel propagation through the GPU backend agree with CPU references") {
    constexpr double tolerance = 1e-5;
    constexpr double distanceMetres = 0.0125;
    auto input = makeField(16, 8);
    fillDeterministic(input);

    auto cpuAsmField = input;
    fft::CpuFftBackend cpuAsmFft;
    propagation::AngularSpectrumPropagator cpuAsm(cpuAsmFft);
    const auto cpuAsmDiagnostics = cpuAsm.propagateInPlace(cpuAsmField, distanceMetres);

    auto gpuAsmField = input;
    fft::OpenGlComputeFftBackend gpuFft;
    propagation::OpenGlAngularSpectrumPropagator gpuAsm(gpuFft);
    const auto gpuAsmDiagnostics = gpuAsm.propagateInPlace(gpuAsmField, distanceMetres);
    CHECK(gpuAsmDiagnostics.propagatingBinCount == cpuAsmDiagnostics.propagatingBinCount);
    CHECK(gpuAsmDiagnostics.evanescentBinCount == cpuAsmDiagnostics.evanescentBinCount);
    checkFieldsNear(gpuAsmField, cpuAsmField, tolerance);

    auto cpuFresnelField = input;
    fft::CpuFftBackend cpuFresnelFft;
    propagation::FresnelPropagator cpuFresnel(cpuFresnelFft);
    const auto cpuFresnelDiagnostics = cpuFresnel.propagateInPlace(
        cpuFresnelField,
        distanceMetres);

    auto gpuFresnelField = input;
    propagation::FresnelPropagator gpuFresnel(gpuFft);
    const auto gpuFresnelDiagnostics = gpuFresnel.propagateInPlace(
        gpuFresnelField,
        distanceMetres);
    CHECK(gpuFresnelDiagnostics.propagatedBinCount
        == cpuFresnelDiagnostics.propagatedBinCount);
    CHECK(gpuFresnelDiagnostics.nonPropagatingBinCount
        == cpuFresnelDiagnostics.nonPropagatingBinCount);
    CHECK(gpuFresnelDiagnostics.transferFunctionUndersampled
        == cpuFresnelDiagnostics.transferFunctionUndersampled);
    checkFieldsNear(gpuFresnelField, cpuFresnelField, tolerance);

    gpuFft.releaseGpuResources();
    CHECK_FALSE(gpuFft.hasGpuResources());
    CHECK(glGetError() == GL_NO_ERROR);
}

TEST_CASE("4-f Fourier planes filter diagnostics and image agree with the CPU reference") {
    constexpr double fieldTolerance = 1e-5;
    constexpr double diagnosticsTolerance = 2e-5;
    auto input = makeField(16, 8);
    fillDeterministic(input);
    const auto filter = fourier::CircularFourierFilter::bandPass(0.25e-3, 1.30e-3);

    fft::CpuFftBackend cpuFft;
    fourier::FourFSystem cpuSystem(cpuFft);
    const auto expected = cpuSystem.run(input, 0.050, 0.075, filter);

    fft::OpenGlComputeFftBackend gpuFft;
    fourier::FourFSystem gpuSystem(gpuFft);
    const auto actual = gpuSystem.run(input, 0.050, 0.075, filter);

    checkFieldsNear(
        actual.fourierPlaneBeforeFilter,
        expected.fourierPlaneBeforeFilter,
        fieldTolerance);
    checkFieldsNear(
        actual.fourierPlaneAfterFilter,
        expected.fourierPlaneAfterFilter,
        fieldTolerance);
    checkFieldsNear(actual.imagePlane, expected.imagePlane, fieldTolerance);
    CHECK(actual.filterDiagnostics.kind == expected.filterDiagnostics.kind);
    CHECK(actual.filterDiagnostics.transmittedSampleCount
        == expected.filterDiagnostics.transmittedSampleCount);
    CHECK(actual.filterDiagnostics.blockedSampleCount
        == expected.filterDiagnostics.blockedSampleCount);
    CHECK(actual.filterDiagnostics.integratedIntensityTransmission
        == doctest::Approx(expected.filterDiagnostics.integratedIntensityTransmission)
            .epsilon(diagnosticsTolerance));
    CHECK(actual.imagePlane.pitchXMetres()
        == doctest::Approx(expected.imagePlane.pitchXMetres()).epsilon(2e-15));
    CHECK(actual.imagePlane.pitchYMetres()
        == doctest::Approx(expected.imagePlane.pitchYMetres()).epsilon(2e-15));

    gpuFft.releaseGpuResources();
    CHECK_FALSE(gpuFft.hasGpuResources());
    CHECK(glGetError() == GL_NO_ERROR);
}

TEST_CASE("explicit release waits for the owning context and preserves external GL bindings") {
    fft::OpenGlComputeFftBackend backend;
    auto value = makeField(8, 4);
    fillDeterministic(value);

    std::array<GLuint, 4> externalBuffers {0, 0, 0, 0};
    glGenBuffers(static_cast<GLsizei>(externalBuffers.size()), externalBuffers.data());
    for (const GLuint buffer : externalBuffers) {
        REQUIRE(buffer != 0);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffer);
        constexpr std::array<double, 2> storage {0.0, 0.0};
        glBufferData(
            GL_SHADER_STORAGE_BUFFER,
            static_cast<GLsizeiptr>(sizeof(storage)),
            storage.data(),
            GL_DYNAMIC_COPY);
    }
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0U, externalBuffers[0]);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1U, externalBuffers[1]);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2U, externalBuffers[2]);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3U, externalBuffers[3]);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, externalBuffers[3]);
    REQUIRE(glGetError() == GL_NO_ERROR);

    backend.forward2D(value);
    REQUIRE(backend.hasGpuResources());

    GLint genericBinding = 0;
    std::array<GLint, 4> indexedBindings {0, 0, 0, 0};
    glGetIntegerv(GL_SHADER_STORAGE_BUFFER_BINDING, &genericBinding);
    for (std::size_t index = 0; index < indexedBindings.size(); ++index) {
        glGetIntegeri_v(
            GL_SHADER_STORAGE_BUFFER_BINDING,
            static_cast<GLuint>(index),
            &indexedBindings[index]);
    }
    CHECK(static_cast<GLuint>(genericBinding) == externalBuffers[3]);
    CHECK(static_cast<GLuint>(indexedBindings[0]) == externalBuffers[0]);
    CHECK(static_cast<GLuint>(indexedBindings[1]) == externalBuffers[1]);
    CHECK(static_cast<GLuint>(indexedBindings[2]) == externalBuffers[2]);
    CHECK(static_cast<GLuint>(indexedBindings[3]) == externalBuffers[3]);

    {
        ScopedNoCurrentContext noContext;
        REQUIRE(noContext.unbound());
        backend.releaseGpuResources();
        CHECK(backend.hasGpuResources());
    }
    REQUIRE(fft::OpenGlComputeFftBackend::isContextAvailable());
    backend.releaseGpuResources();
    CHECK_FALSE(backend.hasGpuResources());

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0U, 0);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1U, 0);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2U, 0);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3U, 0);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    glDeleteBuffers(static_cast<GLsizei>(externalBuffers.size()), externalBuffers.data());
    CHECK(glGetError() == GL_NO_ERROR);
}

TEST_CASE("invalid input and OpenGL failures use distinct exceptions with strong field safety") {
    fft::OpenGlComputeFftBackend backend;

    auto unsupported = makeField(3, 4);
    fillDeterministic(unsupported);
    const auto unsupportedBefore = unsupported;
    CHECK_THROWS_AS(backend.forward2D(unsupported), std::invalid_argument);
    CHECK(fieldsExactlyEqual(unsupported, unsupportedBefore));

    auto nonFinite = makeField(4, 4);
    fillDeterministic(nonFinite);
    nonFinite.at(1, 2) = {std::numeric_limits<double>::infinity(), 0.0};
    const auto nonFiniteBefore = nonFinite;
    CHECK_THROWS_AS(backend.forward2D(nonFinite), std::invalid_argument);
    CHECK(fieldsExactlyEqual(nonFinite, nonFiniteBefore));

    auto fp32Subnormal = makeField(1, 1);
    fp32Subnormal.at(0, 0) = {
        0.5 * static_cast<double>(std::numeric_limits<float>::min()),
        0.0};
    const auto fp32SubnormalBefore = fp32Subnormal;
    CHECK_THROWS_AS(backend.forward2D(fp32Subnormal), std::underflow_error);
    CHECK(fieldsExactlyEqual(fp32Subnormal, fp32SubnormalBefore));

    auto fp32Overflow = makeField(1, 1);
    fp32Overflow.at(0, 0) = {std::numeric_limits<double>::max(), 0.0};
    const auto fp32OverflowBefore = fp32Overflow;
    CHECK_THROWS_AS(backend.forward2D(fp32Overflow), std::overflow_error);
    CHECK(fieldsExactlyEqual(fp32Overflow, fp32OverflowBefore));

    auto invalidTransferField = makeField(1, 1);
    invalidTransferField.at(0, 0) = {1.0, 0.0};
    const auto invalidTransferBefore = invalidTransferField;
    const std::array<std::complex<double>, 1> subnormalTransfer {{
        {0.5 * static_cast<double>(std::numeric_limits<float>::min()), 0.0}}};
    CHECK_THROWS_AS(
        backend.applySpectralTransfer2D(invalidTransferField, subnormalTransfer),
        std::underflow_error);
    CHECK(fieldsExactlyEqual(invalidTransferField, invalidTransferBefore));

    auto glFailure = makeField(4, 4);
    fillDeterministic(glFailure);
    const auto glFailureBefore = glFailure;
    glEnable(GL_TRIANGLES);
    CHECK_THROWS_AS(backend.forward2D(glFailure), fft::OpenGlFftError);
    CHECK(fieldsExactlyEqual(glFailure, glFailureBefore));
    CHECK(glGetError() == GL_NO_ERROR);
}

} // TEST_SUITE("OpenGL compute FFT")

int main(int argc, char** argv) {
    if (!initializeGpuTestContext()) {
        return 77;
    }

    doctest::Context context(argc, argv);
    context.setOption("no-breaks", true);
    const int result = context.run();
    const GLenum finalError = glGetError();
    shutdownGpuTestContext();
    if (finalError != GL_NO_ERROR) {
        std::fprintf(stderr, "GPU tests left OpenGL error 0x%x\n", static_cast<unsigned int>(finalError));
        return 1;
    }
    return result;
}
