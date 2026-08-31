#include "optics/analysis/ChromaticAnalysis.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_set>

namespace holobench::optics::analysis {

std::vector<SpectralLine> makeFraunhoferFdcSpectrum() {
  return {
      {.label = "F (blue)",
       .vacuumWavelengthMetres = 486.1327e-9,
       .powerFraction = 1.0 / 3.0},
      {.label = "d (green)",
       .vacuumWavelengthMetres = 587.5618e-9,
       .powerFraction = 1.0 / 3.0},
      {.label = "C (red)",
       .vacuumWavelengthMetres = 656.2725e-9,
       .powerFraction = 1.0 / 3.0},
  };
}

std::vector<ray::Ray>
expandRayBundleSpectrum(const std::vector<ray::Ray> &baseRays,
                        const std::vector<SpectralLine> &spectrum) {
  if (baseRays.empty() || spectrum.empty()) {
    throw std::invalid_argument(
        "spectral expansion requires rays and spectral lines");
  }
  long double fractionSum = 0.0L;
  std::unordered_set<std::string> labels;
  for (const SpectralLine &line : spectrum) {
    if (line.label.empty() || !labels.insert(line.label).second ||
        !std::isfinite(line.vacuumWavelengthMetres) ||
        line.vacuumWavelengthMetres <= 0.0 ||
        !std::isfinite(line.powerFraction) || line.powerFraction < 0.0) {
      throw std::invalid_argument(
          "spectral lines require unique labels and finite positive "
          "wavelengths/non-negative power");
    }
    fractionSum += line.powerFraction;
  }
  if (std::abs(fractionSum - 1.0L) > 2e-15L) {
    throw std::invalid_argument("spectral power fractions must sum to one");
  }
  if (baseRays.size() >
      std::numeric_limits<std::size_t>::max() / spectrum.size()) {
    throw std::overflow_error("expanded spectral ray count overflows size_t");
  }
  std::vector<ray::Ray> expanded;
  expanded.reserve(baseRays.size() * spectrum.size());
  for (const ray::Ray &base : baseRays) {
    for (const SpectralLine &line : spectrum) {
      expanded.push_back(ray::makeRay(base.originMetres, base.direction,
                                      line.vacuumWavelengthMetres,
                                      base.power * line.powerFraction));
    }
  }
  return expanded;
}

AxialFocusFit fitAxialBestFocus(const std::vector<ray::Ray> &worldRays,
                                const math::RigidTransform3d &frame,
                                double minimumPlaneZMetres,
                                double maximumPlaneZMetres) {
  math::validateRigidTransform(frame);
  if (!std::isfinite(minimumPlaneZMetres) ||
      !std::isfinite(maximumPlaneZMetres) ||
      maximumPlaneZMetres < minimumPlaneZMetres) {
    throw std::invalid_argument(
        "focus-fit plane bounds must be finite and ordered");
  }
  if (worldRays.size() < 2) {
    return {.status = AxialFocusFitStatus::InsufficientRays,
            .rayCount = worldRays.size()};
  }
  struct Line final {
    long double ax;
    long double ay;
    long double bx;
    long double by;
  };
  std::vector<Line> lines;
  lines.reserve(worldRays.size());
  for (const ray::Ray &worldRay : worldRays) {
    const auto origin =
        math::transformPointWorldToLocal(frame, worldRay.originMetres);
    const auto direction =
        math::transformDirectionWorldToLocal(frame, worldRay.direction);
    if (std::abs(direction.z) <=
        64.0 * std::numeric_limits<double>::epsilon()) {
      continue;
    }
    const long double bx = direction.x / direction.z;
    const long double by = direction.y / direction.z;
    lines.push_back(
        {origin.x - bx * origin.z, origin.y - by * origin.z, bx, by});
  }
  if (lines.size() < 2) {
    return {.status = AxialFocusFitStatus::InsufficientRays,
            .rayCount = lines.size()};
  }
  long double meanAx = 0, meanAy = 0, meanBx = 0, meanBy = 0;
  for (const Line &line : lines) {
    meanAx += line.ax;
    meanAy += line.ay;
    meanBx += line.bx;
    meanBy += line.by;
  }
  const long double count = static_cast<long double>(lines.size());
  meanAx /= count;
  meanAy /= count;
  meanBx /= count;
  meanBy /= count;
  long double quadratic = 0, linearHalf = 0;
  for (const Line &line : lines) {
    const long double dax = line.ax - meanAx, day = line.ay - meanAy;
    const long double dbx = line.bx - meanBx, dby = line.by - meanBy;
    quadratic += dbx * dbx + dby * dby;
    linearHalf += dax * dbx + day * dby;
  }
  quadratic /= count;
  linearHalf /= count;
  if (quadratic <= 256.0L * std::numeric_limits<double>::epsilon()) {
    return {.status = AxialFocusFitStatus::Collimated,
            .rayCount = lines.size()};
  }
  const double unconstrained = static_cast<double>(-linearHalf / quadratic);
  const double plane =
      std::clamp(unconstrained, minimumPlaneZMetres, maximumPlaneZMetres);
  long double radiusSquared = 0;
  for (const Line &line : lines) {
    const long double x = line.ax + line.bx * plane,
                      y = line.ay + line.by * plane;
    const long double cx = meanAx + meanBx * plane,
                      cy = meanAy + meanBy * plane;
    radiusSquared += (x - cx) * (x - cx) + (y - cy) * (y - cy);
  }
  return {
      .status = (plane == unconstrained) ? AxialFocusFitStatus::BestFocus
                                         : AxialFocusFitStatus::BoundaryLimited,
      .planeZMetres = plane,
      .rmsRadiusMetres = std::sqrt(static_cast<double>(radiusSquared / count)),
      .rayCount = lines.size(),
  };
}

LongitudinalChromaticResult analyzeLongitudinalChromaticFocus(
    const std::vector<ray::Ray> &incidentRays,
    const ray::SequentialLensPrescription &prescription,
    const math::RigidTransform3d &frame,
    const ray::SurfaceIntersectionOptions &options, double minimumZ,
    double maximumZ) {
  if (incidentRays.empty())
    throw std::invalid_argument("chromatic analysis requires incident rays");
  LongitudinalChromaticResult result;
  std::vector<std::vector<ray::Ray>> completed;
  for (const ray::Ray &incident : incidentRays) {
    auto group = std::find_if(
        result.wavelengthResults.begin(), result.wavelengthResults.end(),
        [&](const auto &value) {
          return value.vacuumWavelengthMetres == incident.wavelengthMetres;
        });
    if (group == result.wavelengthResults.end()) {
      result.wavelengthResults.push_back({
          .vacuumWavelengthMetres = incident.wavelengthMetres,
          .focus = {},
          .rejectedRayCount = 0,
      });
      completed.emplace_back();
      group = std::prev(result.wavelengthResults.end());
    }
    const std::size_t index =
        static_cast<std::size_t>(group - result.wavelengthResults.begin());
    const auto trace =
        ray::traceSequentialLens(incident, prescription, options);
    if (trace.status == ray::SequentialTraceStatus::Completed && trace.finalRay)
      completed[index].push_back(*trace.finalRay);
    else
      ++group->rejectedRayCount;
  }
  bool haveFocus = false;
  double minFocus = 0, maxFocus = 0;
  for (std::size_t i = 0; i < result.wavelengthResults.size(); ++i) {
    auto &value = result.wavelengthResults[i];
    value.focus = fitAxialBestFocus(completed[i], frame, minimumZ, maximumZ);
    if (value.focus.status == AxialFocusFitStatus::BestFocus ||
        value.focus.status == AxialFocusFitStatus::BoundaryLimited) {
      if (!haveFocus) {
        minFocus = maxFocus = value.focus.planeZMetres;
        haveFocus = true;
      } else {
        minFocus = std::min(minFocus, value.focus.planeZMetres);
        maxFocus = std::max(maxFocus, value.focus.planeZMetres);
      }
    }
  }
  result.focalShiftMetres = haveFocus ? maxFocus - minFocus : 0.0;
  return result;
}

} // namespace holobench::optics::analysis
