#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "core/math/RigidTransform.hpp"
#include "optics/ray/SequentialLens.hpp"

namespace holobench::optics::analysis {

struct SpectralLine final {
  std::string label;
  double vacuumWavelengthMetres = 0.0;
  double powerFraction = 0.0;
};

[[nodiscard]] std::vector<SpectralLine> makeFraunhoferFdcSpectrum();

[[nodiscard]] std::vector<ray::Ray>
expandRayBundleSpectrum(const std::vector<ray::Ray> &baseRays,
                        const std::vector<SpectralLine> &spectrum);

enum class AxialFocusFitStatus {
  BestFocus,
  BoundaryLimited,
  Collimated,
  InsufficientRays,
};

struct AxialFocusFit final {
  AxialFocusFitStatus status = AxialFocusFitStatus::InsufficientRays;
  double planeZMetres = 0.0;
  double rmsRadiusMetres = 0.0;
  std::size_t rayCount = 0;
};

[[nodiscard]] AxialFocusFit
fitAxialBestFocus(const std::vector<ray::Ray> &worldRays,
                  const math::RigidTransform3d &analysisFrameLocalToWorld,
                  double minimumPlaneZMetres, double maximumPlaneZMetres);

struct WavelengthFocusResult final {
  double vacuumWavelengthMetres = 0.0;
  AxialFocusFit focus;
  std::size_t rejectedRayCount = 0;
};

struct LongitudinalChromaticResult final {
  std::vector<WavelengthFocusResult> wavelengthResults;
  double focalShiftMetres = 0.0;
};

[[nodiscard]] LongitudinalChromaticResult analyzeLongitudinalChromaticFocus(
    const std::vector<ray::Ray> &incidentWorldRays,
    const ray::SequentialLensPrescription &prescription,
    const math::RigidTransform3d &analysisFrameLocalToWorld,
    const ray::SurfaceIntersectionOptions &intersectionOptions,
    double minimumPlaneZMetres, double maximumPlaneZMetres);

} // namespace holobench::optics::analysis
