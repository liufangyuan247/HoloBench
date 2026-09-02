#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include "core/math/RigidTransform.hpp"
#include "optics/ray/SequentialLens.hpp"

namespace holobench::optics::wave {

struct SequentialPupilRaySample final {
    std::size_t sourceRayIndex = 0;
    ray::Ray exitRay;
    double opticalPathToExitMetres = 0.0;
    double imageXMetres = 0.0;
    double imageYMetres = 0.0;

};

struct SequentialPupilWavefront final {
    std::size_t inputRayCount = 0;
    std::size_t rejectedRayCount = 0;
    std::vector<SequentialPupilRaySample> samples;
    double acceptedPower = 0.0;
    double geometricCentroidXMetres = 0.0;
    double geometricCentroidYMetres = 0.0;
    double geometricRmsRadiusMetres = 0.0;
    double geometricRadiusMetres = 0.0;
    double referenceOpticalPathMetres = 0.0;
    double rmsOpticalPathDifferenceMetres = 0.0;
    double peakToValleyOpticalPathDifferenceMetres = 0.0;
};

// Traces one mutually coherent pupil bundle through the exact sequential
// prescription. Optical-path diagnostics are evaluated at the declared point
// on the arbitrarily placed image plane; the persisted PCG mesh is not used.
[[nodiscard]] SequentialPupilWavefront traceSequentialPupilWavefront(
    std::span<const ray::Ray> incidentWorldRays,
    const ray::SequentialLensPrescription& prescription,
    const math::RigidTransform3d& imagePlaneLocalToWorld,
    double referenceImageXMetres,
    double referenceImageYMetres,
    const ray::SurfaceIntersectionOptions& intersectionOptions,
    std::span<const double> incidentOpticalPathOffsetsMetres = {});

struct CoherentPupilEvaluation final {
    std::vector<double> relativeIntensities;
    double intensitySum = 0.0;
    std::size_t complexTermCount = 0;
    bool operator==(const CoherentPupilEvaluation&) const = default;
};

// Evaluates scalar Huygens superposition from the traced exit wavefront at
// explicit world-space sensor points. Values are intentionally unnormalised;
// a caller normalises over its declared finite support and thereby keeps
// sensor-edge loss explicit.
[[nodiscard]] CoherentPupilEvaluation evaluateCoherentPupil(
    const SequentialPupilWavefront& wavefront,
    std::span<const math::Vec3d> sensorWorldPoints);

} // namespace holobench::optics::wave
