#include <doctest/doctest.h>

#include <cmath>
#include <limits>
#include <stdexcept>
#include <variant>

#include "optics/material/OpticalMaterial.hpp"

namespace material = holobench::optics::material;

namespace {

template <typename Exception, typename Function>
void checkThrowsWithoutDiscarding(Function&& function) {
    CHECK_THROWS_AS(static_cast<void>(function()), Exception);
}

} // namespace

TEST_CASE("constant optical material is wavelength independent on its inclusive domain") {
    const material::OpticalMaterial glass {
        .id = "constant_test_glass",
        .displayName = "Constant test glass",
        .wavelengthDomain = {.minimumMetres = 400e-9, .maximumMetres = 700e-9},
        .dispersion = material::ConstantIndexModel {.refractiveIndex = 1.625},
    };
    CHECK_NOTHROW(material::validateOpticalMaterial(glass));
    CHECK(material::refractiveIndexAtVacuumWavelength(glass, 400e-9) == 1.625);
    CHECK(material::refractiveIndexAtVacuumWavelength(glass, 532e-9) == 1.625);
    CHECK(material::refractiveIndexAtVacuumWavelength(glass, 700e-9) == 1.625);
}

TEST_CASE("SI Cauchy model agrees with an independent hand calculation") {
    constexpr double a = 1.49;
    constexpr double b = 4.2e-15;
    constexpr double c = 1.7e-28;
    const material::OpticalMaterial cauchy {
        .id = "cauchy_test",
        .displayName = "Cauchy test",
        .wavelengthDomain = {.minimumMetres = 400e-9, .maximumMetres = 800e-9},
        .dispersion = material::CauchyModelSi {
            .aDimensionless = a,
            .bSquareMetres = b,
            .cFourthMetres = c,
        },
    };

    for (const double wavelength : {450e-9, 532e-9, 650e-9}) {
        const double lambda2 = wavelength * wavelength;
        const double expected = a + b / lambda2 + c / (lambda2 * lambda2);
        const double actual = material::refractiveIndexAtVacuumWavelength(cauchy, wavelength);
        CHECK(actual == doctest::Approx(expected).epsilon(2e-15));
    }
    CHECK(material::refractiveIndexAtVacuumWavelength(cauchy, 450e-9)
        > material::refractiveIndexAtVacuumWavelength(cauchy, 650e-9));
}

TEST_CASE("SCHOTT N-BK7 SI Sellmeier conversion matches catalog Fraunhofer lines") {
    const material::OpticalMaterial nbk7 = material::makeSchottNBk7Material();
    CHECK_NOTHROW(material::validateOpticalMaterial(nbk7));

    struct Reference final {
        double wavelengthMetres;
        double refractiveIndex;
    };
    const Reference references[] {
        {486.1327e-9, 1.5223762897312285},
        {587.5618e-9, 1.5168000345005885},
        {656.2725e-9, 1.5143223472613747},
        {1.0e-6, 1.5075022039849080},
    };
    for (const Reference& reference : references) {
        const double actual = material::refractiveIndexAtVacuumWavelength(nbk7, reference.wavelengthMetres);
        CHECK(actual == doctest::Approx(reference.refractiveIndex).epsilon(3e-15));
    }
}

TEST_CASE("Sellmeier evaluation preserves signed contributions and rejects non-physical n squared") {
    const material::OpticalMaterial valid {
        .id = "signed_sellmeier",
        .displayName = "Signed Sellmeier",
        .wavelengthDomain = {.minimumMetres = 500e-9, .maximumMetres = 700e-9},
        .dispersion = material::SellmeierModelSi {.terms = {
            {.bDimensionless = 0.8, .cSquareMetres = 0.04e-12},
            {.bDimensionless = -0.05, .cSquareMetres = -0.02e-12},
        }},
    };
    const double wavelength = 600e-9;
    const double lambda2 = wavelength * wavelength;
    const double expectedSquared = 1.0
        + 0.8 * lambda2 / (lambda2 - 0.04e-12)
        - 0.05 * lambda2 / (lambda2 + 0.02e-12);
    CHECK(material::refractiveIndexAtVacuumWavelength(valid, wavelength)
        == doctest::Approx(std::sqrt(expectedSquared)).epsilon(2e-15));

    auto invalid = valid;
    invalid.dispersion = material::SellmeierModelSi {.terms = {
        {.bDimensionless = -2.0, .cSquareMetres = 0.0},
    }};
    checkThrowsWithoutDiscarding<std::domain_error>([&] {
        return material::refractiveIndexAtVacuumWavelength(invalid, wavelength);
    });
}

TEST_CASE("material wavelength domains and Sellmeier poles fail explicitly") {
    const material::OpticalMaterial nbk7 = material::makeSchottNBk7Material();
    checkThrowsWithoutDiscarding<std::out_of_range>([&] {
        return material::refractiveIndexAtVacuumWavelength(nbk7, 299e-9);
    });
    checkThrowsWithoutDiscarding<std::out_of_range>([&] {
        return material::refractiveIndexAtVacuumWavelength(nbk7, 2.6e-6);
    });
    checkThrowsWithoutDiscarding<std::invalid_argument>([&] {
        return material::refractiveIndexAtVacuumWavelength(
            nbk7, std::numeric_limits<double>::quiet_NaN());
    });

    material::OpticalMaterial poleInsideDomain {
        .id = "pole",
        .displayName = "Pole",
        .wavelengthDomain = {.minimumMetres = 400e-9, .maximumMetres = 800e-9},
        .dispersion = material::SellmeierModelSi {.terms = {
            {.bDimensionless = 1.0, .cSquareMetres = 0.36e-12},
        }},
    };
    CHECK_THROWS_AS(material::validateOpticalMaterial(poleInsideDomain), std::invalid_argument);
    poleInsideDomain.wavelengthDomain = {.minimumMetres = 600e-9, .maximumMetres = 700e-9};
    checkThrowsWithoutDiscarding<std::domain_error>([&] {
        return material::refractiveIndexAtVacuumWavelength(poleInsideDomain, 600e-9);
    });
}

TEST_CASE("material validation rejects malformed identities domains and coefficients") {
    auto value = material::makeVacuumMaterial();
    CHECK_NOTHROW(material::validateOpticalMaterial(value));
    CHECK(material::refractiveIndexAtVacuumWavelength(value, 532e-9) == 1.0);

    value.id.clear();
    CHECK_THROWS_AS(material::validateOpticalMaterial(value), std::invalid_argument);
    value = material::makeVacuumMaterial();
    value.wavelengthDomain.minimumMetres = -1.0;
    CHECK_THROWS_AS(material::validateOpticalMaterial(value), std::invalid_argument);
    checkThrowsWithoutDiscarding<std::invalid_argument>([&] {
        return material::refractiveIndexAtVacuumWavelength(value, 532e-9);
    });
    value = material::makeVacuumMaterial();
    value.dispersion = material::ConstantIndexModel {.refractiveIndex = 0.0};
    CHECK_THROWS_AS(material::validateOpticalMaterial(value), std::invalid_argument);
    value.dispersion = material::CauchyModelSi {
        .aDimensionless = 1.5,
        .bSquareMetres = std::numeric_limits<double>::infinity(),
        .cFourthMetres = 0.0,
    };
    CHECK_THROWS_AS(material::validateOpticalMaterial(value), std::invalid_argument);
    value.dispersion = material::SellmeierModelSi {};
    CHECK_THROWS_AS(material::validateOpticalMaterial(value), std::invalid_argument);
    value.dispersion = material::SellmeierModelSi {.terms = {
        {.bDimensionless = 1.0, .cSquareMetres = -1e-12},
        {.bDimensionless = 2.0, .cSquareMetres = -1e-12},
    }};
    CHECK_THROWS_AS(material::validateOpticalMaterial(value), std::invalid_argument);
}
