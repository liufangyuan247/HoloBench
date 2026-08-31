#include "compute/sampling/SamplingDiagnostics.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/field/ComplexField2D.hpp"

namespace holobench::compute::sampling {
namespace {

void requireHalfAngle(double angle, const char* name) {
    if (!std::isfinite(angle) || angle < 0.0 || angle >= std::numbers::pi / 2.0) {
        throw std::invalid_argument(std::string(name) + " must be finite and in [0, pi/2)");
    }
}

[[nodiscard]] double checkedProduct(double first, double second, const char* name) {
    if (!std::isfinite(first) || !std::isfinite(second)) {
        throw std::overflow_error(std::string(name) + " contains a non-finite factor");
    }
    const long double product = static_cast<long double>(first) * static_cast<long double>(second);
    if (!std::isfinite(product)
        || std::abs(product) > static_cast<long double>(std::numeric_limits<double>::max())) {
        throw std::overflow_error(std::string(name) + " exceeds the finite double domain");
    }
    const double result = static_cast<double>(product);
    if (first != 0.0 && second != 0.0 && result == 0.0) {
        throw std::underflow_error(std::string(name) + " underflows the double domain");
    }
    return result;
}

[[nodiscard]] bool coordinateContained(double coordinate, double halfExtent) noexcept {
    const double scale = std::max(std::abs(coordinate), halfExtent);
    const double allowance = 16.0 * std::numeric_limits<double>::epsilon() * scale;
    return std::abs(coordinate) <= halfExtent + allowance;
}

void validateSupportClaim(
    const field::ComplexField2D& field,
    double extentX,
    double extentY) {
    const double halfX = 0.5 * extentX;
    const double halfY = 0.5 * extentY;
    for (std::size_t y = 0; y < field.height(); ++y) {
        for (std::size_t x = 0; x < field.width(); ++x) {
            const auto& sample = field.at(x, y);
            if (!std::isfinite(sample.real()) || !std::isfinite(sample.imag())) {
                throw std::invalid_argument("Sampling analysis requires finite field samples");
            }
            if ((sample.real() != 0.0 || sample.imag() != 0.0)
                && (!coordinateContained(field.xCoordinateMetres(x), halfX)
                    || !coordinateContained(field.yCoordinateMetres(y), halfY))) {
                throw std::invalid_argument(
                    "Sampling illuminated extents exclude a non-zero field sample");
            }
        }
    }
}

} // namespace

SamplingDiagnostics analyzeSampling(
    const field::ComplexField2D& field,
    const SamplingAnalysisOptions& options) {
    if (!std::isfinite(options.propagationDistanceMetres)) {
        throw std::invalid_argument("Sampling propagation distance must be finite");
    }
    requireHalfAngle(options.requestedHalfAngleXRadians, "requested X half-angle");
    requireHalfAngle(options.requestedHalfAngleYRadians, "requested Y half-angle");
    const bool hasExtentX = options.illuminatedExtentXMetres.has_value();
    const bool hasExtentY = options.illuminatedExtentYMetres.has_value();
    if (hasExtentX != hasExtentY) {
        throw std::invalid_argument("Sampling analysis requires both illuminated extents or neither");
    }

    const double physicalWidth = checkedProduct(
        static_cast<double>(field.width()), field.pitchXMetres(), "sampling physical width");
    const double physicalHeight = checkedProduct(
        static_cast<double>(field.height()), field.pitchYMetres(), "sampling physical height");
    const double supportX = hasExtentX ? *options.illuminatedExtentXMetres : physicalWidth;
    const double supportY = hasExtentY ? *options.illuminatedExtentYMetres : physicalHeight;
    if (!std::isfinite(supportX) || supportX <= 0.0 || supportX > physicalWidth
        || !std::isfinite(supportY) || supportY <= 0.0 || supportY > physicalHeight) {
        throw std::invalid_argument(
            "Sampling illuminated extents must be positive, finite, and within the grid");
    }
    validateSupportClaim(field, supportX, supportY);

    const double mediumWavelength = field.vacuumWavelengthMetres() / field.refractiveIndex();
    const double nyquistFrequencyX = 1.0 / (2.0 * field.pitchXMetres());
    const double nyquistFrequencyY = 1.0 / (2.0 * field.pitchYMetres());
    const double maximumSampledFrequencyX = static_cast<double>(field.width() / 2U)
        / physicalWidth;
    const double maximumSampledFrequencyY = static_cast<double>(field.height() / 2U)
        / physicalHeight;
    const double nyquistDirectionCosineX = std::min(1.0, mediumWavelength * nyquistFrequencyX);
    const double nyquistDirectionCosineY = std::min(1.0, mediumWavelength * nyquistFrequencyY);
    const double requestedDirectionCosineX = std::sin(options.requestedHalfAngleXRadians);
    const double requestedDirectionCosineY = std::sin(options.requestedHalfAngleYRadians);

    const double distance = std::abs(options.propagationDistanceMetres);
    const double travelX = checkedProduct(
        distance, std::tan(options.requestedHalfAngleXRadians), "sampling X travel");
    const double travelY = checkedProduct(
        distance, std::tan(options.requestedHalfAngleYRadians), "sampling Y travel");
    const double clearanceX = 0.5 * (physicalWidth - supportX);
    const double clearanceY = 0.5 * (physicalHeight - supportY);
    const double requiredWidth = supportX + 2.0 * travelX;
    const double requiredHeight = supportY + 2.0 * travelY;
    if (!std::isfinite(requiredWidth) || !std::isfinite(requiredHeight)) {
        throw std::overflow_error("Sampling required padded extent exceeds the finite double domain");
    }

    SamplingDiagnostics result;
    result.mediumWavelengthMetres = mediumWavelength;
    result.physicalWidthMetres = physicalWidth;
    result.physicalHeightMetres = physicalHeight;
    result.nyquistHalfAngleXRadians = std::asin(nyquistDirectionCosineX);
    result.nyquistHalfAngleYRadians = std::asin(nyquistDirectionCosineY);
    result.requestedHalfAngleXRadians = options.requestedHalfAngleXRadians;
    result.requestedHalfAngleYRadians = options.requestedHalfAngleYRadians;
    result.maximumSampledRadialFrequencyCyclesPerMetre = std::hypot(
        maximumSampledFrequencyX, maximumSampledFrequencyY);
    result.propagatingCutoffFrequencyCyclesPerMetre = 1.0 / mediumWavelength;
    result.boundaryClearanceXMetres = clearanceX;
    result.boundaryClearanceYMetres = clearanceY;
    result.requiredPaddingFactorX = std::max(1.0, requiredWidth / physicalWidth);
    result.requiredPaddingFactorY = std::max(1.0, requiredHeight / physicalHeight);
    result.spatialAliasingRisk = requestedDirectionCosineX > nyquistDirectionCosineX
        || requestedDirectionCosineY > nyquistDirectionCosineY;
    result.angularBandwidthInsufficient = result.spatialAliasingRisk;
    result.wrapAroundRisk = options.periodicBoundary
        && (travelX > clearanceX || travelY > clearanceY);
    result.insufficientPadding = requiredWidth > physicalWidth || requiredHeight > physicalHeight;
    const double guardX = checkedProduct(
        static_cast<double>(options.minimumBoundaryGuardSamples),
        field.pitchXMetres(),
        "sampling X boundary guard");
    const double guardY = checkedProduct(
        static_cast<double>(options.minimumBoundaryGuardSamples),
        field.pitchYMetres(),
        "sampling Y boundary guard");
    result.apertureTooCloseToBoundary = clearanceX < guardX || clearanceY < guardY;
    result.containsEvanescentBins = result.maximumSampledRadialFrequencyCyclesPerMetre
        > result.propagatingCutoffFrequencyCyclesPerMetre;
    result.periodicBoundary = options.periodicBoundary;

    std::vector<std::string> warnings;
    if (result.spatialAliasingRisk) {
        warnings.emplace_back("Requested angular field exceeds the sampled Nyquist angle (aliasing risk).");
    }
    if (result.wrapAroundRisk) {
        warnings.emplace_back("Periodic propagation can wrap illuminated content across the grid boundary.");
    }
    if (result.insufficientPadding) {
        warnings.emplace_back("Grid padding is insufficient for the requested support and angular travel.");
    }
    if (result.apertureTooCloseToBoundary) {
        warnings.emplace_back("Illuminated support is too close to the grid boundary guard.");
    }
    if (result.containsEvanescentBins) {
        warnings.emplace_back("The sampled spectrum includes evanescent spatial-frequency bins.");
    }
    for (std::size_t index = 0; index < warnings.size(); ++index) {
        if (index != 0U) {
            result.warning += '\n';
        }
        result.warning += warnings[index];
    }
    return result;
}

} // namespace holobench::compute::sampling
