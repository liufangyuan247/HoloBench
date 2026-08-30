#pragma once

#include <cstddef>
#include <vector>

#include "core/math/Vec3.hpp"
#include "optics/scene/OpticalBenchScene.hpp"

namespace holobench::optics::ray {

enum class RaySegmentKind {
    Incident,          // Physical ray from point source to lens/aperture plane
    Transmitted,       // Forward physical ray from lens towards screen/boundary
    Clipped,           // Ray stopped at aperture or lens boundary (no forward transmission)
    VirtualExtension,  // Non-physical backward virtual ray extension for virtual image visualization
};

struct RaySegment final {
    math::Vec3d startMetres {};
    math::Vec3d endMetres {};
    double wavelengthMetres = 532e-9;
    double power = 1.0;
    RaySegmentKind kind = RaySegmentKind::Incident;
    std::size_t rayIndex = 0;

    bool operator==(const RaySegment&) const = default;
};

enum class RaySamplingPattern {
    FibonacciDisk,     // Quasi-uniform Fermat/Fibonacci spiral across circular aperture
    ConcentricRings,   // Concentric rings with radial arms
    MeridionalFan,     // 1D fan along vertical Y axis
    SagittalFan,       // 1D fan along horizontal X axis
    CrossFans,         // Cross of Meridional and Sagittal fans
    ApertureBoundary,  // Perimeter sample points at aperture edge
};

struct BenchTracerOptions final {
    std::size_t rayCount = 64;
    RaySamplingPattern pattern = RaySamplingPattern::FibonacciDisk;
    double maxPropagationDistanceMetres = 2.0;
    bool includeVirtualExtensions = true;
    double virtualExtensionDistanceMetres = 1.0;
};

void traceBench(
    const scene::OpticalBenchScene& scene,
    const BenchTracerOptions& options,
    std::vector<RaySegment>& outSegments);

[[nodiscard]] std::vector<RaySegment> traceBench(
    const scene::OpticalBenchScene& scene,
    const BenchTracerOptions& options = {});

} // namespace holobench::optics::ray
