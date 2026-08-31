#pragma once

#include <string>
#include <variant>
#include <vector>

namespace holobench::optics::material {

struct VacuumWavelengthDomain final {
    double minimumMetres = 380e-9;
    double maximumMetres = 780e-9;

    bool operator==(const VacuumWavelengthDomain&) const = default;
};

struct ConstantIndexModel final {
    double refractiveIndex = 1.0;

    bool operator==(const ConstantIndexModel&) const = default;
};

/** n(lambda) = A + B/lambda^2 + C/lambda^4, with lambda in metres. */
struct CauchyModelSi final {
    double aDimensionless = 1.0;
    double bSquareMetres = 0.0;
    double cFourthMetres = 0.0;

    bool operator==(const CauchyModelSi&) const = default;
};

struct SellmeierTermSi final {
    double bDimensionless = 0.0;
    double cSquareMetres = 0.0;

    bool operator==(const SellmeierTermSi&) const = default;
};

/** n(lambda)^2 = 1 + sum(B_i lambda^2 / (lambda^2 - C_i)). */
struct SellmeierModelSi final {
    std::vector<SellmeierTermSi> terms;

    bool operator==(const SellmeierModelSi&) const = default;
};

using DispersionModel = std::variant<ConstantIndexModel, CauchyModelSi, SellmeierModelSi>;

struct OpticalMaterial final {
    std::string id = "vacuum";
    std::string displayName = "Vacuum";
    VacuumWavelengthDomain wavelengthDomain {};
    DispersionModel dispersion = ConstantIndexModel {};

    bool operator==(const OpticalMaterial&) const = default;
};

void validateOpticalMaterial(const OpticalMaterial& material);

[[nodiscard]] double refractiveIndexAtVacuumWavelength(
    const OpticalMaterial& material,
    double vacuumWavelengthMetres);

[[nodiscard]] OpticalMaterial makeVacuumMaterial();

/** Schott N-BK7 catalog coefficients converted from micrometre squared to SI. */
[[nodiscard]] OpticalMaterial makeSchottNBk7Material();

} // namespace holobench::optics::material
