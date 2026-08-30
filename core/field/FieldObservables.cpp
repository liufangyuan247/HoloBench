#include "core/field/FieldObservables.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>

namespace holobench::field {
namespace {

void requirePositiveFinite(double value, const char* name) {
    if (!std::isfinite(value) || value <= 0.0) {
        throw std::invalid_argument(std::string(name) + " must be positive and finite");
    }
}

void requireNonNegativeFinite(double value, const char* name) {
    if (!std::isfinite(value) || value < 0.0) {
        throw std::invalid_argument(std::string(name) + " must be non-negative and finite");
    }
}

void requireNonPositiveFinite(double value, const char* name) {
    if (!std::isfinite(value) || value > 0.0) {
        throw std::invalid_argument(std::string(name) + " must be non-positive (<= 0) and finite");
    }
}

void requireFiniteSample(const std::complex<double>& sample, std::size_t index) {
    if (!std::isfinite(sample.real()) || !std::isfinite(sample.imag())) {
        throw std::invalid_argument(
            "complex field contains non-finite sample at index " + std::to_string(index));
    }
}

double calculateIntensityChecked(const std::complex<double>& sample, std::size_t index) {
    requireFiniteSample(sample, index);
    const double intensity = std::norm(sample);
    if (!std::isfinite(intensity)) {
        throw std::overflow_error(
            "intensity calculation overflowed double precision at index " + std::to_string(index));
    }
    return intensity;
}

} // namespace

ScalarField2D computeIntensity(const ComplexField2D& field) {
    auto result = ScalarField2D::createMatching(field);
    const auto sourceSamples = field.samples();
    auto destSamples = result.samples();
    const std::size_t count = sourceSamples.size();

    for (std::size_t i = 0; i < count; ++i) {
        destSamples[i] = calculateIntensityChecked(sourceSamples[i], i);
    }
    return result;
}

ScalarField2D computeDecibelIntensity(
    const ComplexField2D& field,
    double floorDecibels,
    double referenceIntensity) {
    requireNonPositiveFinite(floorDecibels, "floor decibels");
    requirePositiveFinite(referenceIntensity, "reference intensity");

    auto result = ScalarField2D::createMatching(field);
    const auto sourceSamples = field.samples();
    auto destSamples = result.samples();
    const std::size_t count = sourceSamples.size();
    const double log10Ref = std::log10(referenceIntensity);

    for (std::size_t i = 0; i < count; ++i) {
        const double intensity = calculateIntensityChecked(sourceSamples[i], i);
        if (intensity == 0.0) {
            destSamples[i] = floorDecibels;
        } else {
            const double logDiff = std::log10(intensity) - log10Ref;
            const double dbValue = 10.0 * logDiff;
            if (!std::isfinite(dbValue)) {
                throw std::overflow_error(
                    "decibel intensity calculation overflowed double precision at index " + std::to_string(i));
            }
            destSamples[i] = std::max(dbValue, floorDecibels);
        }
    }
    return result;
}

PhaseResult computeWrappedPhase(const ComplexField2D& field, double minimumIntensity) {
    requireNonNegativeFinite(minimumIntensity, "minimum intensity");

    auto phaseField = ScalarField2D::createMatching(field);
    const auto sourceSamples = field.samples();
    auto phaseSamples = phaseField.samples();
    const std::size_t count = sourceSamples.size();

    std::vector<std::uint8_t> validity(count, 0);

    constexpr double pi = std::numbers::pi;

    for (std::size_t i = 0; i < count; ++i) {
        const auto& sample = sourceSamples[i];
        const double intensity = calculateIntensityChecked(sample, i);

        // Exact zero or below threshold has undefined phase
        if (intensity == 0.0 || intensity < minimumIntensity) {
            phaseSamples[i] = 0.0;
            validity[i] = 0;
            continue;
        }

        // Calculate normalized phase in [-pi, +pi)
        double angle = 0.0;
        if (sample.real() < 0.0 && sample.imag() == 0.0) {
            // Unify negative real axis (+0.0 or -0.0 imag) to -pi
            angle = -pi;
        } else {
            angle = std::atan2(sample.imag(), sample.real());
            if (angle >= pi || angle < -pi) {
                angle = -pi;
            }
        }

        phaseSamples[i] = angle;
        validity[i] = 1;
    }

    return PhaseResult(std::move(phaseField), std::move(validity));
}

double computeIntegratedIntensity(const ComplexField2D& field) {
    const auto sourceSamples = field.samples();
    const std::size_t count = sourceSamples.size();

    double sum = 0.0;
    for (std::size_t i = 0; i < count; ++i) {
        const double intensity = calculateIntensityChecked(sourceSamples[i], i);
        sum += intensity;
        if (!std::isfinite(sum)) {
            throw std::overflow_error("integrated intensity accumulation overflowed double precision");
        }
    }

    const double dArea = field.pitchXMetres() * field.pitchYMetres();
    const double totalIntensity = sum * dArea;
    if (!std::isfinite(totalIntensity)) {
        throw std::overflow_error("integrated intensity scaling overflowed double precision");
    }
    return totalIntensity;
}

double computeIntegratedIntensity(const ScalarField2D& intensityField) {
    const auto sourceSamples = intensityField.samples();
    const std::size_t count = sourceSamples.size();

    double sum = 0.0;
    for (std::size_t i = 0; i < count; ++i) {
        const double val = sourceSamples[i];
        if (!std::isfinite(val) || val < 0.0) {
            throw std::invalid_argument(
                "intensity field contains invalid (negative or non-finite) sample at index " + std::to_string(i));
        }
        sum += val;
        if (!std::isfinite(sum)) {
            throw std::overflow_error("integrated intensity accumulation overflowed double precision");
        }
    }

    const double dArea = intensityField.pitchXMetres() * intensityField.pitchYMetres();
    const double totalIntensity = sum * dArea;
    if (!std::isfinite(totalIntensity)) {
        throw std::overflow_error("integrated intensity scaling overflowed double precision");
    }
    return totalIntensity;
}

} // namespace holobench::field
