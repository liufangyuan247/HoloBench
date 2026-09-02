#pragma once

#include <complex>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "core/math/Vec3.hpp"
#include "optics/scene/BenchScene.hpp"

namespace holobench::field {
class ComplexField2D;
}

namespace holobench::compute::fft {
class IFftBackend;
}

namespace holobench::optics::wave {

struct DiffuseObjectPrimitiveFrame final {
    math::Vec3d centreInSourceMetres {};
    math::Vec3d xAxisInSource {1.0, 0.0, 0.0};
    math::Vec3d yAxisInSource {0.0, 1.0, 0.0};
    math::Vec3d zAxisInSource {0.0, 0.0, 1.0};
};

// Shared parameter-derived pose for diagnostics and disposable PCG visuals.
// Analytic intersection below remains the optical source of truth.
[[nodiscard]] DiffuseObjectPrimitiveFrame diffuseObjectPrimitiveFrame(
    const scene::ObjectWavefrontSourceParameters& parameters);

[[nodiscard]] math::Vec3d diffuseObjectPrimitiveToSourcePoint(
    const DiffuseObjectPrimitiveFrame& frame,
    math::Vec3d pointInPrimitiveMetres) noexcept;

struct DiffuseObjectSurfaceSample final {
    math::Vec3d positionInSourceMetres {};
    math::Vec3d outwardNormalInSource {0.0, 0.0, 1.0};
    math::Vec3d positionInPrimitiveMetres {};
    double lambertianAmplitude = 0.0;

    bool operator==(const DiffuseObjectSurfaceSample&) const = default;
};

// Casts a source-local ray along -Z and returns the nearest visible analytic
// primitive surface. Rendering triangles are deliberately not consulted.
[[nodiscard]] std::optional<DiffuseObjectSurfaceSample>
sampleDiffuseObjectSurface(
    const scene::ObjectWavefrontSourceParameters& parameters,
    double sourceXMetres,
    double sourceYMetres);

// Stable physical-space rough phase used to model an opaque coherent diffuse
// surface. Identical geometry, seed, and position produce identical phase.
[[nodiscard]] double diffuseObjectRoughPhaseRadians(
    std::uint64_t seed,
    math::Vec3d positionInPrimitiveMetres);

struct DiffuseObjectWavefrontDiagnostics final {
    std::size_t visibleSurfaceSampleCount = 0U;
    std::size_t populatedDepthLayerCount = 0U;
    double nearestSurfaceDepthMetres = 0.0;
    double farthestSurfaceDepthMetres = 0.0;
    double normalizedPowerWatts = 0.0;
};

// Replaces referenceField with the primitive's scalar object wave at its
// source reference plane. Visible surface samples are quantized into bounded
// depth layers, propagated to z=0 with ASM, coherently summed, and normalized
// to totalScatteredPowerWatts inside the represented working window.
[[nodiscard]] DiffuseObjectWavefrontDiagnostics
synthesizeDiffuseObjectWavefrontAtReferencePlane(
    field::ComplexField2D& referenceField,
    const scene::ObjectWavefrontSourceParameters& parameters,
    double totalScatteredPowerWatts,
    std::complex<double> sourcePhase,
    compute::fft::IFftBackend& fftBackend);

} // namespace holobench::optics::wave
