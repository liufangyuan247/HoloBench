#include "optics/holography/VolumeHologram.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace holobench::optics::holography {
namespace {

[[nodiscard]] double clampEfficiency(double value) {
    if (!std::isfinite(value)) {
        throw std::overflow_error("Kogelnik efficiency is not representable");
    }
    return std::clamp(value, 0.0, 1.0);
}

void validateParameters(const VolumeHologramParameters& parameters) {
    constexpr double kHalfPi = std::numbers::pi_v<double> * 0.5;
    if (!std::isfinite(parameters.recordedThicknessMetres)
        || parameters.recordedThicknessMetres <= 0.0
        || !std::isfinite(parameters.averageRefractiveIndex)
        || parameters.averageRefractiveIndex <= 0.0
        || !std::isfinite(parameters.refractiveIndexModulation)
        || parameters.refractiveIndexModulation < 0.0
        || parameters.refractiveIndexModulation
            >= parameters.averageRefractiveIndex
        || !std::isfinite(parameters.recordingVacuumWavelengthMetres)
        || parameters.recordingVacuumWavelengthMetres <= 0.0
        || !std::isfinite(parameters.replayVacuumWavelengthMetres)
        || parameters.replayVacuumWavelengthMetres <= 0.0
        || !std::isfinite(parameters.recordingBraggAngleInMediumRadians)
        || parameters.recordingBraggAngleInMediumRadians < 0.0
        || parameters.recordingBraggAngleInMediumRadians >= kHalfPi
        || !std::isfinite(parameters.replayAngleInMediumRadians)
        || std::abs(parameters.replayAngleInMediumRadians) >= kHalfPi
        || !std::isfinite(parameters.isotropicLinearShrinkageFraction)
        || parameters.isotropicLinearShrinkageFraction < 0.0
        || parameters.isotropicLinearShrinkageFraction >= 1.0) {
        throw std::invalid_argument(
            "volume hologram parameters must be finite and physical");
    }
    if (parameters.geometry == VolumeHologramGeometry::Transmission
        && parameters.recordingBraggAngleInMediumRadians == 0.0) {
        throw std::invalid_argument(
            "transmission volume grating requires a nonzero recording Bragg angle");
    }
}

[[nodiscard]] double exactBraggEfficiency(
    VolumeHologramGeometry geometry,
    double couplingStrength) noexcept {
    if (geometry == VolumeHologramGeometry::Transmission) {
        const double sine = std::sin(couplingStrength);
        return sine * sine;
    }
    const double tangent = std::tanh(couplingStrength);
    return tangent * tangent;
}

} // namespace

KogelnikEfficiencyResult evaluateKogelnikEfficiency(
    VolumeHologramGeometry geometry,
    double couplingStrength,
    double detuningParameter) {
    if (!std::isfinite(couplingStrength) || couplingStrength < 0.0
        || !std::isfinite(detuningParameter)) {
        throw std::invalid_argument(
            "Kogelnik coupling and detuning must be finite with non-negative coupling");
    }
    if (couplingStrength == 0.0) {
        return {
            .diffractionEfficiency = 0.0,
            .couplingStrength = couplingStrength,
            .detuningParameter = detuningParameter,
        };
    }

    double efficiency = 0.0;
    const double couplingSquared = couplingStrength * couplingStrength;
    const double detuningSquared = detuningParameter * detuningParameter;
    if (!std::isfinite(couplingSquared) || !std::isfinite(detuningSquared)) {
        throw std::overflow_error(
            "Kogelnik dimensionless parameters exceed the representable domain");
    }

    if (geometry == VolumeHologramGeometry::Transmission) {
        const double root = std::hypot(couplingStrength, detuningParameter);
        const double sine = std::sin(root);
        const double ratio = couplingStrength / root;
        efficiency = ratio * ratio * sine * sine;
    } else {
        const double discriminant = couplingSquared - detuningSquared;
        const double scale = std::max(couplingSquared, detuningSquared);
        const double boundaryTolerance
            = 32.0 * std::numeric_limits<double>::epsilon()
            * std::max(1.0, scale);
        if (std::abs(discriminant) <= boundaryTolerance) {
            efficiency = couplingSquared / (couplingSquared + 1.0);
        } else if (discriminant > 0.0) {
            const double root = std::sqrt(discriminant);
            if (root > 350.0) {
                efficiency = 1.0;
            } else {
                const double hyperbolicSine = std::sinh(root);
                const double numerator = hyperbolicSine * hyperbolicSine;
                efficiency = numerator
                    / (numerator + discriminant / couplingSquared);
            }
        } else {
            const double magnitude = -discriminant;
            const double sine = std::sin(std::sqrt(magnitude));
            const double numerator = sine * sine;
            efficiency = numerator
                / (numerator + magnitude / couplingSquared);
        }
    }

    return {
        .diffractionEfficiency = clampEfficiency(efficiency),
        .couplingStrength = couplingStrength,
        .detuningParameter = detuningParameter,
    };
}

VolumeHologramResult evaluateVolumeHologram(
    const VolumeHologramParameters& parameters) {
    validateParameters(parameters);
    constexpr double kTwoPi = 2.0 * std::numbers::pi_v<double>;
    const double shrinkScale
        = 1.0 - parameters.isotropicLinearShrinkageFraction;
    const double replayThickness
        = parameters.recordedThicknessMetres * shrinkScale;
    const double recordingWavenumber = kTwoPi
        * parameters.averageRefractiveIndex
        / parameters.recordingVacuumWavelengthMetres;
    const double replayWavenumber = kTwoPi
        * parameters.averageRefractiveIndex
        / parameters.replayVacuumWavelengthMetres;
    if (!std::isfinite(replayThickness) || replayThickness <= 0.0
        || !std::isfinite(recordingWavenumber)
        || !std::isfinite(replayWavenumber)) {
        throw std::overflow_error(
            "volume hologram physical scale is not representable");
    }

    double recordedGratingWavenumber = 0.0;
    if (parameters.geometry == VolumeHologramGeometry::Transmission) {
        recordedGratingWavenumber = 2.0 * recordingWavenumber
            * std::sin(parameters.recordingBraggAngleInMediumRadians);
    } else {
        recordedGratingWavenumber = 2.0 * recordingWavenumber
            * std::cos(parameters.recordingBraggAngleInMediumRadians);
    }
    const double replayGratingWavenumber
        = recordedGratingWavenumber / shrinkScale;
    const double recordedPeriod = kTwoPi / recordedGratingWavenumber;
    const double replayPeriod = recordedPeriod * shrinkScale;
    if (!std::isfinite(recordedGratingWavenumber)
        || recordedGratingWavenumber <= 0.0
        || !std::isfinite(replayGratingWavenumber)
        || !std::isfinite(recordedPeriod) || recordedPeriod <= 0.0
        || !std::isfinite(replayPeriod) || replayPeriod <= 0.0) {
        throw std::overflow_error(
            "volume hologram grating period is not representable");
    }

    const double replayCosine
        = std::cos(parameters.replayAngleInMediumRadians);
    double diffractedCosine = replayCosine;
    double diffractedAngle = parameters.replayAngleInMediumRadians;
    double mismatch = 0.0;
    bool propagating = true;
    if (parameters.geometry == VolumeHologramGeometry::Transmission) {
        const double incidentTransverseWavenumber = replayWavenumber
            * std::sin(parameters.replayAngleInMediumRadians);
        const double diffractedTransverseWavenumber
            = incidentTransverseWavenumber - replayGratingWavenumber;
        const double normalizedDiffractedTransverse
            = diffractedTransverseWavenumber / replayWavenumber;
        propagating = std::isfinite(normalizedDiffractedTransverse)
            && std::abs(normalizedDiffractedTransverse) <= 1.0;
        if (propagating) {
            diffractedAngle = std::asin(std::clamp(
                normalizedDiffractedTransverse, -1.0, 1.0));
            diffractedCosine = std::cos(diffractedAngle);
            mismatch = replayWavenumber
                * (diffractedCosine - replayCosine);
        }
    } else {
        mismatch = replayGratingWavenumber
            - 2.0 * replayWavenumber * replayCosine;
    }

    const double cosineProduct = replayCosine * diffractedCosine;
    const double coupling = std::numbers::pi_v<double>
        * parameters.refractiveIndexModulation * replayThickness
        / (parameters.replayVacuumWavelengthMetres
            * std::sqrt(cosineProduct));
    const double detuning = 0.5 * replayThickness * mismatch;
    if (!std::isfinite(coupling) || coupling < 0.0
        || (propagating && !std::isfinite(detuning))) {
        throw std::overflow_error(
            "volume hologram coupling or detuning is not representable");
    }

    VolumeHologramResult result {
        .replayThicknessMetres = replayThickness,
        .recordedGratingPeriodMetres = recordedPeriod,
        .replayGratingPeriodMetres = replayPeriod,
        .replayWavenumberRadiansPerMetre = replayWavenumber,
        .phaseMismatchRadiansPerMetre = propagating ? mismatch : 0.0,
        .diffractedInternalAngleRadians = propagating ? diffractedAngle : 0.0,
        .diffractedOrderPropagating = propagating,
        .kogelnikEfficiencyEvaluated = propagating,
        .kogelnik = {
            .diffractionEfficiency = 0.0,
            .couplingStrength = coupling,
            .detuningParameter = propagating ? detuning : 0.0,
        },
        .exactBraggEfficiencyAtReplayCoupling = exactBraggEfficiency(
            parameters.geometry, coupling),
    };
    if (propagating) {
        result.kogelnik = evaluateKogelnikEfficiency(
            parameters.geometry, coupling, detuning);
    }
    return result;
}

} // namespace holobench::optics::holography
