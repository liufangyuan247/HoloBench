#include "core/field/FieldObservables.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

namespace holobench::field {
namespace {

void requirePositiveFinite(double value, const char* name) {
    if (!std::isfinite(value) || value <= 0.0) {
        throw std::invalid_argument(std::string(name) + " must be positive and finite");
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

ScalarField2D computeNaturalLogIntensity(const ComplexField2D& field, double floorValue) {
    requirePositiveFinite(floorValue, "floor value");

    auto result = ScalarField2D::createMatching(field);
    const auto sourceSamples = field.samples();
    auto destSamples = result.samples();
    const std::size_t count = sourceSamples.size();

    for (std::size_t i = 0; i < count; ++i) {
        const double intensity = calculateIntensityChecked(sourceSamples[i], i);
        const double clamped = std::max(intensity, floorValue);
        const double logValue = std::log(clamped);
        if (!std::isfinite(logValue)) {
            throw std::overflow_error(
                "natural log intensity calculation overflowed double precision at index " + std::to_string(i));
        }
        destSamples[i] = logValue;
    }
    return result;
}

ScalarField2D computeDecibelIntensity(
    const ComplexField2D& field,
    double floorValue,
    double referenceIntensity) {
    requirePositiveFinite(floorValue, "floor value");
    requirePositiveFinite(referenceIntensity, "reference intensity");

    auto result = ScalarField2D::createMatching(field);
    const auto sourceSamples = field.samples();
    auto destSamples = result.samples();
    const std::size_t count = sourceSamples.size();

    for (std::size_t i = 0; i < count; ++i) {
        const double intensity = calculateIntensityChecked(sourceSamples[i], i);
        const double clamped = std::max(intensity, floorValue);
        const double ratio = clamped / referenceIntensity;
        const double dbValue = 10.0 * std::log10(ratio);
        if (!std::isfinite(dbValue)) {
            throw std::overflow_error(
                "decibel intensity calculation overflowed double precision at index " + std::to_string(i));
        }
        destSamples[i] = dbValue;
    }
    return result;
}

ScalarField2D computeWrappedPhase(const ComplexField2D& field) {
    auto result = ScalarField2D::createMatching(field);
    const auto sourceSamples = field.samples();
    auto destSamples = result.samples();
    const std::size_t count = sourceSamples.size();

    for (std::size_t i = 0; i < count; ++i) {
        const auto& sample = sourceSamples[i];
        requireFiniteSample(sample, i);
        if (sample.real() == 0.0 && sample.imag() == 0.0) {
            destSamples[i] = 0.0;
        } else {
            destSamples[i] = std::atan2(sample.imag(), sample.real());
        }
    }
    return result;
}

double computeIntegratedPower(const ComplexField2D& field) {
    const auto sourceSamples = field.samples();
    const std::size_t count = sourceSamples.size();

    double sum = 0.0;
    for (std::size_t i = 0; i < count; ++i) {
        const double intensity = calculateIntensityChecked(sourceSamples[i], i);
        sum += intensity;
        if (!std::isfinite(sum)) {
            throw std::overflow_error("integrated power accumulation overflowed double precision");
        }
    }

    const double dArea = field.pitchXMetres() * field.pitchYMetres();
    const double totalPower = sum * dArea;
    if (!std::isfinite(totalPower)) {
        throw std::overflow_error("integrated power scaling overflowed double precision");
    }
    return totalPower;
}

double computeIntegratedPower(const ScalarField2D& intensityField) {
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
            throw std::overflow_error("integrated power accumulation overflowed double precision");
        }
    }

    const double dArea = intensityField.pitchXMetres() * intensityField.pitchYMetres();
    const double totalPower = sum * dArea;
    if (!std::isfinite(totalPower)) {
        throw std::overflow_error("integrated power scaling overflowed double precision");
    }
    return totalPower;
}

} // namespace holobench::field
