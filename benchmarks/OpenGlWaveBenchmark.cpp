#include <glad/gl.h>
#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <vector>

#include "compute/fft/OpenGlComputeFftBackend.hpp"
#include "compute/propagation/OpenGlAngularSpectrumPropagator.hpp"
#include "core/field/ComplexField2D.hpp"

namespace fft = holobench::compute::fft;
namespace propagation = holobench::compute::propagation;
namespace field = holobench::field;

namespace {

constexpr std::size_t kGridSize = 1024;
constexpr std::size_t kWarmupCount = 5;
constexpr std::size_t kMeasuredCount = 30;
constexpr double kPitchMetres = 4.0e-6;
constexpr double kVacuumWavelengthMetres = 532.0e-9;
constexpr double kBeamRadiusMetres = 0.65e-3;
constexpr double kDistanceMetres = 0.10;
constexpr double kP95TargetMilliseconds = 50.0;

GLADapiproc loadOpenGlProcedure(const char* name) {
    return reinterpret_cast<GLADapiproc>(SDL_GL_GetProcAddress(name));
}

class HiddenOpenGlContext final {
public:
    HiddenOpenGlContext() {
        if (!SDL_Init(SDL_INIT_VIDEO)) {
            return;
        }
        sdlInitialized_ = true;
        static_cast<void>(SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4));
        static_cast<void>(SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 6));
        static_cast<void>(SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE));
        static_cast<void>(SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG));

        window_ = SDL_CreateWindow(
            "HoloBench GPU wave benchmark",
            64,
            64,
            SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
        if (window_ == nullptr) {
            return;
        }
        context_ = SDL_GL_CreateContext(window_);
        if (context_ == nullptr || !SDL_GL_MakeCurrent(window_, context_)) {
            return;
        }
        const int loadedVersion = gladLoadGL(loadOpenGlProcedure);
        available_ = loadedVersion != 0
            && (GLAD_VERSION_MAJOR(loadedVersion) > 4
                || (GLAD_VERSION_MAJOR(loadedVersion) == 4
                    && GLAD_VERSION_MINOR(loadedVersion) >= 6));
    }

    ~HiddenOpenGlContext() {
        if (context_ != nullptr) {
            static_cast<void>(SDL_GL_MakeCurrent(window_, context_));
            if (available_ && glFinish != nullptr) {
                glFinish();
            }
            SDL_GL_DestroyContext(context_);
        }
        if (window_ != nullptr) {
            SDL_DestroyWindow(window_);
        }
        if (sdlInitialized_) {
            SDL_Quit();
        }
    }

    HiddenOpenGlContext(const HiddenOpenGlContext&) = delete;
    HiddenOpenGlContext& operator=(const HiddenOpenGlContext&) = delete;

    [[nodiscard]] bool available() const noexcept { return available_; }

private:
    SDL_Window* window_ = nullptr;
    SDL_GLContext context_ = nullptr;
    bool sdlInitialized_ = false;
    bool available_ = false;
};

field::ComplexField2D makeBenchmarkField() {
    field::ComplexField2D result(
        kGridSize,
        kGridSize,
        kPitchMetres,
        kPitchMetres,
        kVacuumWavelengthMetres);
    const double inverseRadiusSquared = 1.0 / (kBeamRadiusMetres * kBeamRadiusMetres);
    for (std::size_t y = 0; y < result.height(); ++y) {
        const double yMetres = result.yCoordinateMetres(y);
        for (std::size_t x = 0; x < result.width(); ++x) {
            const double xMetres = result.xCoordinateMetres(x);
            const double amplitude = std::exp(
                -(xMetres * xMetres + yMetres * yMetres) * inverseRadiusSquared);
            result.at(x, y) = {amplitude, 0.0};
        }
    }
    return result;
}

double nearestRankPercentile(std::vector<double> sorted, double percentile) {
    std::sort(sorted.begin(), sorted.end());
    const double rank = std::ceil(percentile * static_cast<double>(sorted.size())) - 1.0;
    const std::size_t index = static_cast<std::size_t>(std::max(0.0, rank));
    return sorted[std::min(index, sorted.size() - 1)];
}

} // namespace

int main() {
    HiddenOpenGlContext context;
    if (!context.available() || !fft::OpenGlComputeFftBackend::isContextAvailable()) {
        std::fprintf(stderr, "GPU benchmark unavailable: OpenGL 4.6 compute context is required\n");
        return 77;
    }

    const auto* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    const auto* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));

    try {
        const auto source = makeBenchmarkField();
        auto working = source;
        fft::OpenGlComputeFftBackend backend;
        propagation::OpenGlAngularSpectrumPropagator propagator(backend);

        for (std::size_t iteration = 0; iteration < kWarmupCount; ++iteration) {
            working = source;
            static_cast<void>(propagator.propagateInPlace(working, kDistanceMetres));
            glFinish();
        }

        std::vector<double> fftRoundTripMilliseconds;
        fftRoundTripMilliseconds.reserve(10);
        for (std::size_t iteration = 0; iteration < 10; ++iteration) {
            working = source;
            glFinish();
            const auto start = std::chrono::steady_clock::now();
            backend.forward2D(working);
            backend.inverse2D(working);
            glFinish();
            const auto finish = std::chrono::steady_clock::now();
            fftRoundTripMilliseconds.push_back(
                std::chrono::duration<double, std::milli>(finish - start).count());
        }

        std::vector<double> milliseconds;
        milliseconds.reserve(kMeasuredCount);
        for (std::size_t iteration = 0; iteration < kMeasuredCount; ++iteration) {
            working = source;
            glFinish();
            const auto start = std::chrono::steady_clock::now();
            static_cast<void>(propagator.propagateInPlace(working, kDistanceMetres));
            glFinish();
            const auto finish = std::chrono::steady_clock::now();
            milliseconds.push_back(std::chrono::duration<double, std::milli>(finish - start).count());
        }

        const double p50 = nearestRankPercentile(milliseconds, 0.50);
        const double p95 = nearestRankPercentile(milliseconds, 0.95);
        const double maximum = *std::max_element(milliseconds.begin(), milliseconds.end());
        const double fftRoundTripP50 = nearestRankPercentile(fftRoundTripMilliseconds, 0.50);
        const bool targetMet = p95 < kP95TargetMilliseconds;

        backend.releaseGpuResources();
        if (glGetError() != GL_NO_ERROR) {
            std::fprintf(stderr, "GPU benchmark failed: OpenGL error remained after the run\n");
            return 1;
        }

        std::printf(
            "benchmark=wave/asm_1024_square_gpu_recompute renderer=\"%s\" version=\"%s\" "
            "grid=%zux%zu pitch_um=%.3f wavelength_nm=%.3f distance_m=%.3f "
            "warmup=%zu samples=%zu gpu_sync=true p50_ms=%.6f p95_ms=%.6f max_ms=%.6f "
            "fft_round_trip_p50_ms=%.6f target_p95_ms=%.3f target_met=%s\n",
            renderer != nullptr ? renderer : "unknown",
            version != nullptr ? version : "unknown",
            kGridSize,
            kGridSize,
            kPitchMetres * 1.0e6,
            kVacuumWavelengthMetres * 1.0e9,
            kDistanceMetres,
            kWarmupCount,
            kMeasuredCount,
            p50,
            p95,
            maximum,
            fftRoundTripP50,
            kP95TargetMilliseconds,
            targetMet ? "true" : "false");
        return targetMet ? 0 : 2;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "GPU benchmark failed: %s\n", error.what());
        return 1;
    }
}
