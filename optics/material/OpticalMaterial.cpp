#include "optics/material/OpticalMaterial.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace holobench::optics::material {

namespace {

constexpr std::size_t kMaximumSellmeierTermCount = 32;

void validateMaterialHeader(const OpticalMaterial& material) {
    if (material.id.empty() || material.displayName.empty()) {
        throw std::invalid_argument("optical material id and display name must be non-empty");
    }
    const auto& domain = material.wavelengthDomain;
    if (!std::isfinite(domain.minimumMetres)
        || !std::isfinite(domain.maximumMetres)
        || domain.minimumMetres <= 0.0
        || domain.maximumMetres < domain.minimumMetres) {
        throw std::invalid_argument("material wavelength domain must be finite, positive, and ordered");
    }
}

[[nodiscard]] double checkedPositiveIndex(long double value) {
    if (!std::isfinite(value)) {
        throw std::overflow_error("refractive index is not finite");
    }
    if (value <= 0.0L) {
        throw std::domain_error("dispersion model produced a non-positive refractive index");
    }
    if (value > static_cast<long double>(std::numeric_limits<double>::max())) {
        throw std::overflow_error("refractive index exceeds the double-precision domain");
    }
    const double converted = static_cast<double>(value);
    if (!std::isfinite(converted) || converted <= 0.0) {
        throw std::underflow_error("refractive index is not representable as a positive double");
    }
    return converted;
}

[[nodiscard]] double evaluateConstant(const ConstantIndexModel& model) {
    return checkedPositiveIndex(static_cast<long double>(model.refractiveIndex));
}

[[nodiscard]] double evaluateCauchy(const CauchyModelSi& model, double wavelengthMetres) {
    const long double wavelength = static_cast<long double>(wavelengthMetres);
    const long double wavelengthSquared = wavelength * wavelength;
    if (wavelengthSquared == 0.0L) {
        throw std::underflow_error("squared wavelength underflows the dispersion domain");
    }

    long double index = static_cast<long double>(model.aDimensionless);
    if (model.bSquareMetres != 0.0) {
        index += static_cast<long double>(model.bSquareMetres) / wavelengthSquared;
    }
    if (model.cFourthMetres != 0.0) {
        const long double wavelengthFourth = wavelengthSquared * wavelengthSquared;
        if (wavelengthFourth == 0.0L) {
            throw std::underflow_error("fourth-power wavelength underflows the Cauchy domain");
        }
        index += static_cast<long double>(model.cFourthMetres) / wavelengthFourth;
    }
    return checkedPositiveIndex(index);
}

[[nodiscard]] double evaluateSellmeier(const SellmeierModelSi& model, double wavelengthMetres) {
    const long double wavelength = static_cast<long double>(wavelengthMetres);
    const long double wavelengthSquared = wavelength * wavelength;
    if (wavelengthSquared == 0.0L) {
        throw std::underflow_error("squared wavelength underflows the dispersion domain");
    }

    long double indexSquared = 1.0L;
    long double compensation = 0.0L;
    for (const SellmeierTermSi& term : model.terms) {
        const long double resonance = static_cast<long double>(term.cSquareMetres);
        const long double denominator = wavelengthSquared - resonance;
        const long double scale = std::max({
            std::abs(wavelengthSquared),
            std::abs(resonance),
            static_cast<long double>(std::numeric_limits<double>::min()),
        });
        const long double poleTolerance = 64.0L
            * static_cast<long double>(std::numeric_limits<double>::epsilon()) * scale;
        if (std::abs(denominator) <= poleTolerance) {
            throw std::domain_error("requested wavelength is at a Sellmeier pole");
        }

        const long double contribution = static_cast<long double>(term.bDimensionless)
            * wavelengthSquared / denominator;
        if (!std::isfinite(contribution)) {
            throw std::overflow_error("Sellmeier contribution is not finite");
        }
        const long double corrected = contribution - compensation;
        const long double next = indexSquared + corrected;
        compensation = (next - indexSquared) - corrected;
        indexSquared = next;
    }
    if (!std::isfinite(indexSquared)) {
        throw std::overflow_error("Sellmeier squared index is not finite");
    }
    if (indexSquared <= 0.0L) {
        throw std::domain_error("Sellmeier model produced non-positive n squared");
    }
    return checkedPositiveIndex(std::sqrt(indexSquared));
}

void validateDispersionModel(const DispersionModel& dispersion) {
    std::visit([](const auto& model) {
        using Model = std::decay_t<decltype(model)>;
        if constexpr (std::is_same_v<Model, ConstantIndexModel>) {
            if (!std::isfinite(model.refractiveIndex) || model.refractiveIndex <= 0.0) {
                throw std::invalid_argument("constant refractive index must be finite and positive");
            }
        } else if constexpr (std::is_same_v<Model, CauchyModelSi>) {
            if (!std::isfinite(model.aDimensionless)
                || !std::isfinite(model.bSquareMetres)
                || !std::isfinite(model.cFourthMetres)) {
                throw std::invalid_argument("Cauchy coefficients must be finite");
            }
        } else {
            if (model.terms.empty() || model.terms.size() > kMaximumSellmeierTermCount) {
                throw std::invalid_argument("Sellmeier model must contain between one and 32 terms");
            }
            for (std::size_t index = 0; index < model.terms.size(); ++index) {
                const SellmeierTermSi& term = model.terms[index];
                if (!std::isfinite(term.bDimensionless) || !std::isfinite(term.cSquareMetres)) {
                    throw std::invalid_argument("Sellmeier coefficients must be finite");
                }
                for (std::size_t previous = 0; previous < index; ++previous) {
                    if (model.terms[previous].cSquareMetres == term.cSquareMetres) {
                        throw std::invalid_argument("Sellmeier resonance coefficients must be unique");
                    }
                }
            }
        }
    }, dispersion);
}

} // namespace

void validateOpticalMaterial(const OpticalMaterial& material) {
    validateMaterialHeader(material);
    const auto& domain = material.wavelengthDomain;
    validateDispersionModel(material.dispersion);

    if (const auto* sellmeier = std::get_if<SellmeierModelSi>(&material.dispersion)) {
        for (const SellmeierTermSi& term : sellmeier->terms) {
            if (term.cSquareMetres <= 0.0) {
                continue;
            }
            const double poleWavelength = std::sqrt(term.cSquareMetres);
            if (poleWavelength >= domain.minimumMetres && poleWavelength <= domain.maximumMetres) {
                throw std::invalid_argument("material wavelength domain contains a Sellmeier pole");
            }
        }
    }

    static_cast<void>(refractiveIndexAtVacuumWavelength(material, domain.minimumMetres));
    static_cast<void>(refractiveIndexAtVacuumWavelength(material, domain.maximumMetres));
}

double refractiveIndexAtVacuumWavelength(
    const OpticalMaterial& material,
    double vacuumWavelengthMetres) {
    if (!std::isfinite(vacuumWavelengthMetres) || vacuumWavelengthMetres <= 0.0) {
        throw std::invalid_argument("vacuum wavelength must be finite and positive");
    }
    validateMaterialHeader(material);
    const auto& domain = material.wavelengthDomain;
    if (vacuumWavelengthMetres < domain.minimumMetres
        || vacuumWavelengthMetres > domain.maximumMetres) {
        throw std::out_of_range("vacuum wavelength is outside the material model domain");
    }
    validateDispersionModel(material.dispersion);

    return std::visit([vacuumWavelengthMetres](const auto& model) -> double {
        using Model = std::decay_t<decltype(model)>;
        if constexpr (std::is_same_v<Model, ConstantIndexModel>) {
            return evaluateConstant(model);
        } else if constexpr (std::is_same_v<Model, CauchyModelSi>) {
            return evaluateCauchy(model, vacuumWavelengthMetres);
        } else {
            return evaluateSellmeier(model, vacuumWavelengthMetres);
        }
    }, material.dispersion);
}

OpticalMaterial makeVacuumMaterial() {
    return OpticalMaterial {
        .id = "vacuum",
        .displayName = "Vacuum",
        .wavelengthDomain = {.minimumMetres = 1e-12, .maximumMetres = 1.0},
        .dispersion = ConstantIndexModel {.refractiveIndex = 1.0},
    };
}

OpticalMaterial makeSchottNBk7Material() {
    constexpr double kMicrometreSquaredToSquareMetres = 1e-12;
    return OpticalMaterial {
        .id = "schott_n_bk7",
        .displayName = "SCHOTT N-BK7",
        .wavelengthDomain = {.minimumMetres = 300e-9, .maximumMetres = 2.5e-6},
        .dispersion = SellmeierModelSi {.terms = {
            {.bDimensionless = 1.03961212, .cSquareMetres = 0.00600069867 * kMicrometreSquaredToSquareMetres},
            {.bDimensionless = 0.231792344, .cSquareMetres = 0.0200179144 * kMicrometreSquaredToSquareMetres},
            {.bDimensionless = 1.01046945, .cSquareMetres = 103.560653 * kMicrometreSquaredToSquareMetres},
        }},
    };
}

} // namespace holobench::optics::material
