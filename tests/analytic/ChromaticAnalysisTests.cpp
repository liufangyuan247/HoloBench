#include <doctest/doctest.h>

#include <cmath>
#include <stdexcept>
#include <vector>

#include "optics/analysis/ChromaticAnalysis.hpp"

namespace analysis = holobench::optics::analysis;
namespace material = holobench::optics::material;
namespace ray = holobench::optics::ray;

TEST_CASE("spectral bundle expansion preserves ray power and Fraunhofer line "
          "identity") {
  const std::vector<ray::Ray> base{
      ray::makeRay({0, 0, 0}, {0, 0, 1}, 532e-9, 3.0),
      ray::makeRay({0.001, 0, 0}, {0, 0, 1}, 532e-9, 6.0),
  };
  const auto spectrum = analysis::makeFraunhoferFdcSpectrum();
  const auto expanded = analysis::expandRayBundleSpectrum(base, spectrum);
  REQUIRE(expanded.size() == 6);
  CHECK(expanded[0].wavelengthMetres == spectrum[0].vacuumWavelengthMetres);
  CHECK(expanded[1].wavelengthMetres == spectrum[1].vacuumWavelengthMetres);
  CHECK(expanded[2].wavelengthMetres == spectrum[2].vacuumWavelengthMetres);
  CHECK(expanded[0].power == doctest::Approx(1.0).epsilon(1e-14));
  CHECK(expanded[3].power == doctest::Approx(2.0).epsilon(1e-14));

  auto invalid = spectrum;
  invalid[0].powerFraction = 0.5;
  const auto invoke = [&] {
    const auto value = analysis::expandRayBundleSpectrum(base, invalid);
    static_cast<void>(value);
  };
  CHECK_THROWS_AS(invoke(), std::invalid_argument);
}

TEST_CASE("analytic axial focus fit recovers an exact converging bundle") {
  const double focusZ = 0.2;
  const std::vector<ray::Ray> rays{
      ray::makeRay({-0.01, 0, 0}, {0.01, 0, focusZ}),
      ray::makeRay({0.0, 0, 0}, {0, 0, 1}),
      ray::makeRay({0.01, 0, 0}, {-0.01, 0, focusZ}),
      ray::makeRay({0, 0.008, 0}, {0, -0.008, focusZ}),
  };
  const auto fit = analysis::fitAxialBestFocus(rays, {}, 0.1, 0.3);
  CHECK(fit.status == analysis::AxialFocusFitStatus::BestFocus);
  CHECK(fit.planeZMetres == doctest::Approx(focusZ).epsilon(2e-14));
  CHECK(fit.rmsRadiusMetres <= 2e-17);

  const auto limited = analysis::fitAxialBestFocus(rays, {}, 0.1, 0.15);
  CHECK(limited.status == analysis::AxialFocusFitStatus::BoundaryLimited);
  CHECK(limited.planeZMetres == 0.15);
}

TEST_CASE("real spherical interface produces catalog-driven longitudinal "
          "chromatic shift") {
  const auto vacuum = material::makeVacuumMaterial();
  const auto nbk7 = material::makeSchottNBk7Material();
  const ray::SequentialLensPrescription prescription{
      .id = "spherical_refracting_surface",
      .materials = {vacuum, nbk7},
      .surfaces = {{
          .id = "front",
          .geometry = {.curvaturePerMetre = 20.0,
                       .conicConstant = 0.0,
                       .evenAsphereTerms = {},
                       .clearSemiDiameterMetres = 0.01},
          .localToWorld = {},
          .materialBeforeId = "vacuum",
          .materialAfterId = "schott_n_bk7",
      }},
  };
  const std::vector<ray::Ray> base{
      ray::makeRay({-0.004, 0, -0.02}, {0, 0, 1}, 532e-9),
      ray::makeRay({0, 0, -0.02}, {0, 0, 1}, 532e-9),
      ray::makeRay({0.004, 0, -0.02}, {0, 0, 1}, 532e-9),
  };
  auto options = ray::SurfaceIntersectionOptions{};
  options.maximumDistanceMetres = 0.1;
  const auto result = analysis::analyzeLongitudinalChromaticFocus(
      analysis::expandRayBundleSpectrum(base,
                                        analysis::makeFraunhoferFdcSpectrum()),
      prescription, {}, options, 0.05, 0.3);
  REQUIRE(result.wavelengthResults.size() == 3);
  CHECK(result.wavelengthResults[0].focus.status ==
        analysis::AxialFocusFitStatus::BestFocus);
  CHECK(result.wavelengthResults[2].focus.status ==
        analysis::AxialFocusFitStatus::BestFocus);
  CHECK(result.wavelengthResults[0].focus.planeZMetres <
        result.wavelengthResults[2].focus.planeZMetres);
  CHECK(result.focalShiftMetres > 0.0);
  CHECK(result.focalShiftMetres < 0.01);
}

TEST_CASE(
    "collimated and insufficient bundles are classified without false focus") {
  const std::vector<ray::Ray> parallel{ray::makeRay({-0.01, 0, 0}, {0, 0, 1}),
                                       ray::makeRay({0.01, 0, 0}, {0, 0, 1})};
  CHECK(analysis::fitAxialBestFocus(parallel, {}, 0, 1).status ==
        analysis::AxialFocusFitStatus::Collimated);
  CHECK(analysis::fitAxialBestFocus({parallel[0]}, {}, 0, 1).status ==
        analysis::AxialFocusFitStatus::InsufficientRays);
}
