#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/math/RigidTransform.hpp"
#include "optics/analysis/ChromaticAnalysis.hpp"
#include "optics/analysis/SpotDiagram.hpp"
#include "optics/io/LensPrescriptionIO.hpp"
#include "optics/ray/SequentialLens.hpp"

#ifndef HOLOBENCH_OPTILAND_VALIDATION_DIR
#error                                                                         \
    "HOLOBENCH_OPTILAND_VALIDATION_DIR must identify the configured validation-data directory"
#endif

namespace {

namespace analysis = holobench::optics::analysis;
namespace io = holobench::optics::io;
namespace math = holobench::math;
namespace ray = holobench::optics::ray;
using Json = nlohmann::json;

constexpr double kIntersectionAbsoluteToleranceMetres = 2e-9;
constexpr double kDirectionAbsoluteTolerance = 2e-8;
constexpr double kOpticalPathAbsoluteToleranceMetres = 3e-9;
constexpr double kSpotAbsoluteToleranceMetres = 5e-9;
constexpr double kFocusPlaneAbsoluteToleranceMetres = 2e-7;
constexpr double kFocusRmsAbsoluteToleranceMetres = 5e-8;
constexpr double kRelativeTolerance = 2e-8;

struct InputRay final {
  std::string id;
  ray::Ray value;
};

struct SurfaceRecord final {
  std::string surfaceId;
  math::Vec3d pointMetres;
  math::Vec3d outgoingDirection;
  double cumulativeOpticalPathMetres = 0.0;
};

struct TracedRay final {
  std::string id;
  double vacuumWavelengthMetres = 0.0;
  math::Vec3d originMetres;
  math::Vec3d direction;
  double cumulativeOpticalPathMetres = 0.0;
  std::vector<SurfaceRecord> records;
};

struct FocusGolden final {
  double vacuumWavelengthMetres = 0.0;
  double planeZMetres = 0.0;
  double rmsRadiusMetres = 0.0;
  std::size_t rayCount = 0;
};

struct SpotGolden final {
  std::size_t sourceRayIndex = 0;
  double vacuumWavelengthMetres = 0.0;
  double xMetres = 0.0;
  double yMetres = 0.0;
};

struct GoldenCase final {
  std::string name;
  std::filesystem::path prescriptionPath;
  std::vector<InputRay> inputRays;
  std::vector<TracedRay> tracedRays;
  std::array<double, 2> focusBoundsMetres{};
  std::vector<FocusGolden> focus;
  double longitudinalFocalShiftMetres = 0.0;
  double imagePlaneZMetres = 0.0;
  std::vector<SpotGolden> spots;
};

[[nodiscard]] std::filesystem::path validationDirectory() {
  return std::filesystem::path(HOLOBENCH_OPTILAND_VALIDATION_DIR);
}

[[nodiscard]] Json loadJson(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("unable to open Optiland validation file: " +
                             path.string());
  }
  return Json::parse(input);
}

[[nodiscard]] math::Vec3d parseVector(const Json &value,
                                      std::string_view context) {
  if (!value.is_array() || value.size() != 3) {
    throw std::runtime_error(std::string(context) +
                             " must contain three coordinates");
  }
  const math::Vec3d result{value.at(0).get<double>(), value.at(1).get<double>(),
                           value.at(2).get<double>()};
  if (!math::isFinite(result)) {
    throw std::runtime_error(std::string(context) + " must be finite");
  }
  return result;
}

[[nodiscard]] GoldenCase loadGoldenCase(std::string_view name) {
  const auto path =
      validationDirectory() / "goldens" / (std::string(name) + ".json");
  const Json document = loadJson(path);
  if (document.at("format").get<std::string>() !=
          "holobench-optiland-real-lens-golden" ||
      document.at("format_version").get<int>() != 1) {
    throw std::runtime_error("unsupported Optiland golden format");
  }
  const Json &generator = document.at("generator");
  if (generator.at("package").get<std::string>() != "optiland" ||
      generator.at("package_version").get<std::string>() != "0.6.2" ||
      generator.at("source_commit").get<std::string>() !=
          "019413c2d8a2a367b7f6f7e8c422c8f76d6eb5ad") {
    throw std::runtime_error("Optiland golden generator identity drifted");
  }

  GoldenCase result;
  result.name = document.at("name").get<std::string>();
  result.prescriptionPath = validationDirectory() /
                            document.at("prescription_file").get<std::string>();
  for (const Json &input : document.at("input_rays")) {
    result.inputRays.push_back({
        .id = input.at("id").get<std::string>(),
        .value = ray::makeRay(
            parseVector(input.at("origin_m"), "golden input origin"),
            parseVector(input.at("direction"), "golden input direction"),
            input.at("vacuum_wavelength_m").get<double>(),
            input.at("power").get<double>()),
    });
  }
  for (const Json &traced : document.at("traced_rays")) {
    TracedRay value{
        .id = traced.at("id").get<std::string>(),
        .vacuumWavelengthMetres =
            traced.at("vacuum_wavelength_m").get<double>(),
        .originMetres =
            parseVector(traced.at("origin_m"), "golden final origin"),
        .direction =
            parseVector(traced.at("direction"), "golden final direction"),
        .cumulativeOpticalPathMetres =
            traced.at("cumulative_optical_path_m").get<double>(),
        .records = {},
    };
    for (const Json &record : traced.at("surface_records")) {
      value.records.push_back({
          .surfaceId = record.at("surface_id").get<std::string>(),
          .pointMetres =
              parseVector(record.at("point_m"), "golden surface point"),
          .outgoingDirection = parseVector(record.at("outgoing_direction"),
                                           "golden outgoing direction"),
          .cumulativeOpticalPathMetres =
              record.at("cumulative_optical_path_m").get<double>(),
      });
    }
    result.tracedRays.push_back(std::move(value));
  }
  const Json &analysisValue = document.at("analysis");
  result.focusBoundsMetres = {
      analysisValue.at("focus_bounds_m").at(0).get<double>(),
      analysisValue.at("focus_bounds_m").at(1).get<double>(),
  };
  for (const Json &focus : analysisValue.at("best_focus_by_wavelength")) {
    result.focus.push_back({
        .vacuumWavelengthMetres = focus.at("vacuum_wavelength_m").get<double>(),
        .planeZMetres = focus.at("plane_z_m").get<double>(),
        .rmsRadiusMetres = focus.at("rms_radius_m").get<double>(),
        .rayCount = focus.at("ray_count").get<std::size_t>(),
    });
  }
  result.longitudinalFocalShiftMetres =
      analysisValue.at("longitudinal_focal_shift_m").get<double>();
  result.imagePlaneZMetres = analysisValue.at("image_plane_z_m").get<double>();
  for (const Json &spot : analysisValue.at("spot_samples")) {
    result.spots.push_back({
        .sourceRayIndex = spot.at("source_ray_index").get<std::size_t>(),
        .vacuumWavelengthMetres = spot.at("vacuum_wavelength_m").get<double>(),
        .xMetres = spot.at("x_m").get<double>(),
        .yMetres = spot.at("y_m").get<double>(),
    });
  }
  if (result.name != name ||
      result.inputRays.size() != result.tracedRays.size() ||
      result.inputRays.size() != result.spots.size()) {
    throw std::runtime_error("inconsistent Optiland golden ray counts");
  }
  return result;
}

[[nodiscard]] ray::SurfaceIntersectionOptions intersectionOptions() {
  auto result = ray::SurfaceIntersectionOptions{};
  result.maximumDistanceMetres = 0.6;
  result.residualToleranceMetres = 1e-13;
  result.maximumIterations = 128;
  result.bracketSubdivisions = 2048;
  return result;
}

void checkNear(double actual, double expected, double absoluteTolerance,
               std::string_view observable) {
  const double tolerance =
      absoluteTolerance + kRelativeTolerance * std::abs(expected);
  INFO("observable: " << observable);
  INFO("actual: " << actual << ", expected: " << expected
                  << ", tolerance: " << tolerance);
  CHECK(std::abs(actual - expected) <= tolerance);
}

void checkVector(math::Vec3d actual, math::Vec3d expected,
                 double absoluteTolerance, std::string_view observable) {
  checkNear(actual.x, expected.x, absoluteTolerance, observable);
  checkNear(actual.y, expected.y, absoluteTolerance, observable);
  checkNear(actual.z, expected.z, absoluteTolerance, observable);
}

void checkTrace(const GoldenCase &golden,
                const ray::SequentialLensPrescription &prescription) {
  REQUIRE(golden.inputRays.size() == golden.tracedRays.size());
  for (std::size_t index = 0; index < golden.inputRays.size(); ++index) {
    const InputRay &input = golden.inputRays[index];
    const TracedRay &expected = golden.tracedRays[index];
    INFO("case: " << golden.name << ", ray: " << input.id);
    REQUIRE(input.id == expected.id);
    const auto actual = ray::traceSequentialLens(input.value, prescription,
                                                 intersectionOptions());
    REQUIRE(actual.status == ray::SequentialTraceStatus::Completed);
    REQUIRE(actual.finalRay.has_value());
    REQUIRE(actual.records.size() == expected.records.size());
    for (std::size_t recordIndex = 0; recordIndex < actual.records.size();
         ++recordIndex) {
      const auto &actualRecord = actual.records[recordIndex];
      const auto &expectedRecord = expected.records[recordIndex];
      INFO("surface: " << expectedRecord.surfaceId);
      REQUIRE(actualRecord.surfaceId == expectedRecord.surfaceId);
      REQUIRE(actualRecord.outgoingRay.has_value());
      checkVector(actualRecord.worldPointMetres, expectedRecord.pointMetres,
                  kIntersectionAbsoluteToleranceMetres, "surface intersection");
      checkVector(actualRecord.outgoingRay->direction,
                  expectedRecord.outgoingDirection, kDirectionAbsoluteTolerance,
                  "outgoing direction");
      checkNear(actualRecord.cumulativeOpticalPathMetres,
                expectedRecord.cumulativeOpticalPathMetres,
                kOpticalPathAbsoluteToleranceMetres, "cumulative optical path");
    }
    checkVector(actual.finalRay->originMetres, expected.originMetres,
                kIntersectionAbsoluteToleranceMetres, "final origin");
    checkVector(actual.finalRay->direction, expected.direction,
                kDirectionAbsoluteTolerance, "final direction");
    checkNear(actual.totalOpticalPathMetres,
              expected.cumulativeOpticalPathMetres,
              kOpticalPathAbsoluteToleranceMetres, "total optical path");
  }
}

void checkAnalysis(const GoldenCase &golden,
                   const ray::SequentialLensPrescription &prescription) {
  std::vector<ray::Ray> inputs;
  inputs.reserve(golden.inputRays.size());
  for (const InputRay &value : golden.inputRays) {
    inputs.push_back(value.value);
  }
  const auto options = intersectionOptions();
  const math::RigidTransform3d imagePlane{
      .translationMetres = {0.0, 0.0, golden.imagePlaneZMetres}};
  const auto spot =
      analysis::computeSpotDiagram(inputs, prescription, imagePlane, options);
  REQUIRE(spot.rejectedRays.empty());
  REQUIRE(spot.samples.size() == golden.spots.size());
  for (std::size_t index = 0; index < spot.samples.size(); ++index) {
    const auto &actual = spot.samples[index];
    const auto &expected = golden.spots[index];
    INFO("case: " << golden.name << ", spot index: " << index);
    REQUIRE(actual.sourceRayIndex == expected.sourceRayIndex);
    REQUIRE(actual.vacuumWavelengthMetres == expected.vacuumWavelengthMetres);
    checkNear(actual.imageXMetres, expected.xMetres,
              kSpotAbsoluteToleranceMetres, "image-plane spot x");
    checkNear(actual.imageYMetres, expected.yMetres,
              kSpotAbsoluteToleranceMetres, "image-plane spot y");
  }

  const auto chromatic = analysis::analyzeLongitudinalChromaticFocus(
      inputs, prescription, math::RigidTransform3d{}, options,
      golden.focusBoundsMetres[0], golden.focusBoundsMetres[1]);
  REQUIRE(chromatic.wavelengthResults.size() == golden.focus.size());
  for (const FocusGolden &expected : golden.focus) {
    const auto actual = std::find_if(
        chromatic.wavelengthResults.begin(), chromatic.wavelengthResults.end(),
        [&](const analysis::WavelengthFocusResult &candidate) {
          return candidate.vacuumWavelengthMetres ==
                 expected.vacuumWavelengthMetres;
        });
    REQUIRE(actual != chromatic.wavelengthResults.end());
    INFO("case: " << golden.name
                  << ", wavelength: " << expected.vacuumWavelengthMetres);
    REQUIRE(actual->focus.status == analysis::AxialFocusFitStatus::BestFocus);
    REQUIRE(actual->focus.rayCount == expected.rayCount);
    REQUIRE(actual->rejectedRayCount == 0);
    checkNear(actual->focus.planeZMetres, expected.planeZMetres,
              kFocusPlaneAbsoluteToleranceMetres, "best-focus plane");
    checkNear(actual->focus.rmsRadiusMetres, expected.rmsRadiusMetres,
              kFocusRmsAbsoluteToleranceMetres, "best-focus RMS radius");
  }
  checkNear(chromatic.focalShiftMetres, golden.longitudinalFocalShiftMetres,
            kFocusPlaneAbsoluteToleranceMetres, "longitudinal focal shift");
}

void validateCase(std::string_view name) {
  const GoldenCase golden = loadGoldenCase(name);
  const auto prescription =
      io::loadLensPrescriptionJson(golden.prescriptionPath);
  checkTrace(golden, prescription);
  checkAnalysis(golden, prescription);
}

} // namespace

TEST_SUITE("Optiland real-lens external cross-validation") {
  TEST_CASE("plano-convex singlet agrees with pinned Optiland") {
    validateCase("plano_convex_singlet");
  }

  TEST_CASE("positive meniscus agrees with pinned Optiland") {
    validateCase("positive_meniscus");
  }

  TEST_CASE("cemented achromatic doublet agrees with pinned Optiland") {
    validateCase("cemented_achromatic_doublet");
  }

  TEST_CASE("conic singlet agrees with pinned Optiland") {
    validateCase("conic_singlet");
  }

  TEST_CASE("even-asphere singlet agrees with pinned Optiland") {
    validateCase("even_asphere_singlet");
  }
}
