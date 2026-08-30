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
    if (sample.real() == 0.0 && sample.imag() == 0.0) {
        return 0.0;
    }
    const double maxAmp = std::max(std::abs(sample.real()), std::abs(sample.imag()));
    int expM = 0;
    std::frexp(maxAmp, &expM);

    const double u = std::ldexp(sample.real(), -expM);
    const double v = std::ldexp(sample.imag(), -expM);
    const double sumSquares = u * u + v * v;

    int expS = 0;
    const double mS = std::frexp(sumSquares, &expS);
    const int finalExp = expS + 2 * expM;

    if (finalExp < -1073) {
        throw std::underflow_error(
            "intensity calculation underflowed double precision at index " + std::to_string(index));
    }
    if (finalExp > 1024) {
        throw std::overflow_error(
            "intensity calculation overflowed double precision at index " + std::to_string(index));
    }

    const double result = std::ldexp(mS, finalExp);
    if (!std::isfinite(result)) {
        throw std::overflow_error(
            "intensity calculation overflowed double precision at index " + std::to_string(index));
    }
    if (result == 0.0) {
        throw std::underflow_error(
            "intensity calculation underflowed double precision at index " + std::to_string(index));
    }
    return result;
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
        const auto& sample = sourceSamples[i];
        requireFiniteSample(sample, i);

        if (sample.real() == 0.0 && sample.imag() == 0.0) {
            destSamples[i] = floorDecibels;
        } else {
            const double a = std::abs(sample.real());
            const double b = std::abs(sample.imag());
            const double maxComp = std::max(a, b);
            const double minComp = std::min(a, b);
            const double r = minComp / maxComp;
            const double log10Amp = std::log10(maxComp) + 0.5 * std::log10(1.0 + r * r);
            const double dbValue = 20.0 * log10Amp - 10.0 * log10Ref;
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
    const double sqrtThreshold = (minimumIntensity > 0.0) ? std::sqrt(minimumIntensity) : 0.0;

    for (std::size_t i = 0; i < count; ++i) {
        const auto& sample = sourceSamples[i];
        requireFiniteSample(sample, i);

        // Exact zero check
        if (sample.real() == 0.0 && sample.imag() == 0.0) {
            phaseSamples[i] = 0.0;
            validity[i] = 0;
            continue;
        }

        // If threshold > 0, check magnitude against sqrt(threshold)
        if (minimumIntensity > 0.0) {
            const double mag = std::hypot(sample.real(), sample.imag());
            if (mag < sqrtThreshold) {
                phaseSamples[i] = 0.0;
                validity[i] = 0;
                continue;
            }
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

    double maxAmp = 0.0;
    for (std::size_t i = 0; i < count; ++i) {
        const auto& sample = sourceSamples[i];
        requireFiniteSample(sample, i);
        maxAmp = std::max({maxAmp, std::abs(sample.real()), std::abs(sample.imag())});
    }

    if (maxAmp == 0.0) {
        return 0.0;
    }

    int expM = 0;
    std::frexp(maxAmp, &expM);

    double sum = 0.0;
    for (std::size_t i = 0; i < count; ++i) {
        const double u = std::ldexp(sourceSamples[i].real(), -expM);
        const double v = std::ldexp(sourceSamples[i].imag(), -expM);
        sum += (u * u + v * v);
    }

    int expS = 0;
    int expX = 0;
    int expY = 0;
    const double mS = std::frexp(sum, &expS);
    const double mX = std::frexp(field.pitchXMetres(), &expX);
    const double mY = std::frexp(field.pitchYMetres(), &expY);

    const double mProd = mS * mX * mY;
    int expProd = 0;
    const double normProd = std::frexp(mProd, &expProd);
    const int finalExp = expS + 2 * expM + expX + expY + expProd;

    if (finalExp < -1073) {
        throw std::underflow_error("integrated intensity calculation underflowed double precision");
    }
    if (finalExp > 1024) {
        throw std::overflow_error("integrated intensity calculation overflowed double precision");
    }

    const double result = std::ldexp(normProd, finalExp);
    if (!std::isfinite(result)) {
        throw std::overflow_error("integrated intensity calculation overflowed double precision");
    }
    if (result == 0.0) {
        throw std::underflow_error("integrated intensity calculation underflowed double precision");
    }
    return result;
}

double computeIntegratedIntensity(const ScalarField2D& intensityField) {
    const auto sourceSamples = intensityField.samples();
    const std::size_t count = sourceSamples.size();

    double maxVal = 0.0;
    for (std::size_t i = 0; i < count; ++i) {
        const double val = sourceSamples[i];
        if (!std::isfinite(val) || val < 0.0) {
            throw std::invalid_argument(
                "intensity field contains invalid (negative or non-finite) sample at index " + std::to_string(i));
        }
        maxVal = std::max(maxVal, val);
    }

    if (maxVal == 0.0) {
        return 0.0;
    }

    int expM = 0;
    std::frexp(maxVal, &expM);

    double sum = 0.0;
    for (std::size_t i = 0; i < count; ++i) {
        sum += std::ldexp(sourceSamples[i], -expM);
    }

    int expS = 0;
    int expX = 0;
    int expY = 0;
    const double mS = std::frexp(sum, &expS);
    const double mX = std::frexp(intensityField.pitchXMetres(), &expX);
    const double mY = std::frexp(intensityField.pitchYMetres(), &expY);

    const double mProd = mS * mX * mY;
    int expProd = 0;
    const double normProd = std::frexp(mProd, &expProd);
    const int finalExp = expS + expM + expX + expY + expProd;

    if (finalExp < -1073) {
        throw std::underflow_error("integrated intensity calculation underflowed double precision");
    }
    if (finalExp > 1024) {
        throw std::overflow_error("integrated intensity calculation overflowed double precision");
    }

    const double result = std::ldexp(normProd, finalExp);
    if (!std::isfinite(result)) {
        throw std::overflow_error("integrated intensity calculation overflowed double precision");
    }
    if (result == 0.0) {
        throw std::underflow_error("integrated intensity calculation underflowed double precision");
    }
    return result;
}

} // namespace holobench::field
