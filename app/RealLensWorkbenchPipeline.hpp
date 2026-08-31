#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "optics/analysis/ChromaticAnalysis.hpp"
#include "optics/analysis/SpotDiagram.hpp"

namespace holobench::app::reallens {

struct FieldDefinition final {
    std::string id;
    double angleXRadians = 0.0;
    double angleYRadians = 0.0;
    double powerFraction = 1.0;
};

struct RealLensWorkbenchConfig final {
    optics::ray::SequentialLensPrescription prescription;
    std::vector<FieldDefinition> fields;
    std::string chromaticReferenceFieldId = "on_axis";
    std::vector<optics::analysis::SpectralLine> spectrum;
    double entrancePupilSemiDiameterMetres = 0.008;
    double objectSpaceDistanceMetres = 0.02;
    std::size_t pupilRingCount = 4;
    std::size_t pupilSamplesPerFirstRing = 8;
    math::RigidTransform3d imagePlaneLocalToWorld;
    double minimumFocusPlaneZMetres = 0.02;
    double maximumFocusPlaneZMetres = 0.20;
    optics::ray::SurfaceIntersectionOptions intersectionOptions;
};

struct TracePolyline final {
    std::string fieldId;
    double vacuumWavelengthMetres = 0.0;
    optics::ray::SequentialTraceStatus status = optics::ray::SequentialTraceStatus::Miss;
    std::vector<math::Vec3d> worldPointsMetres;
};

struct RealLensWorkbenchResult final {
    std::vector<optics::analysis::FieldTaggedRay> incidentRays;
    optics::analysis::SpotDiagramResult spotDiagram;
    optics::analysis::LongitudinalChromaticResult chromaticFocus;
    std::vector<TracePolyline> tracePolylines;
};

[[nodiscard]] RealLensWorkbenchConfig makeDefaultRealLensWorkbenchConfig();

void validateRealLensWorkbenchConfig(const RealLensWorkbenchConfig& config);

[[nodiscard]] RealLensWorkbenchResult runRealLensWorkbench(
    const RealLensWorkbenchConfig& config);

} // namespace holobench::app::reallens
