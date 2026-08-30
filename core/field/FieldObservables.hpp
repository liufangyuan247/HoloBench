#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

#include "core/field/ComplexField2D.hpp"
#include "core/field/ScalarField2D.hpp"

namespace holobench::field {

/**
 * @brief Represents the principal wrapped phase distribution along with pointwise validity information.
 *
 * Physical phase is undefined where field amplitude is zero or below a measurement/noise threshold.
 * For invalid sample positions, wrappedPhaseRadians is deterministically filled with 0.0, and
 * validityMask contains 0 (invalid). Callers consuming wrappedPhaseRadians must check validityMask
 * or isValid() before interpreting values as physical optical phase.
 */
class PhaseResult final {
public:
    PhaseResult(ScalarField2D wrappedPhaseRadians, std::vector<std::uint8_t> validityMask)
        : wrappedPhaseRadians_(std::move(wrappedPhaseRadians)),
          validityMask_(std::move(validityMask)) {
        if (validityMask_.size() != wrappedPhaseRadians_.sampleCount()) {
            throw std::invalid_argument(
                "validity mask size must match wrapped phase sample count");
        }
    }

    [[nodiscard]] const ScalarField2D& wrappedPhaseRadians() const noexcept {
        return wrappedPhaseRadians_;
    }

    [[nodiscard]] const std::vector<std::uint8_t>& validityMask() const noexcept {
        return validityMask_;
    }

    [[nodiscard]] bool isValid(std::size_t xIndex, std::size_t yIndex) const {
        if (xIndex >= wrappedPhaseRadians_.width() || yIndex >= wrappedPhaseRadians_.height()) {
            throw std::out_of_range("phase result sample coordinate out of range");
        }
        const std::size_t index = yIndex * wrappedPhaseRadians_.width() + xIndex;
        if (index >= validityMask_.size()) {
            throw std::out_of_range("phase result sample index out of range");
        }
        return validityMask_[index] != 0;
    }

    [[nodiscard]] bool isValid(std::size_t flatIndex) const {
        if (flatIndex >= wrappedPhaseRadians_.sampleCount() || flatIndex >= validityMask_.size()) {
            throw std::out_of_range("phase result sample index out of range");
        }
        return validityMask_[flatIndex] != 0;
    }

private:
    ScalarField2D wrappedPhaseRadians_;
    std::vector<std::uint8_t> validityMask_;
};

/**
 * @brief Computes linear intensity I(x, y) = |U(x, y)|^2 = Re(U)^2 + Im(U)^2 for every sample.
 *
 * Exact zero samples (0.0 + 0.0i) evaluate to 0.0. If a non-zero sample has a true mathematical intensity
 * strictly below double-precision denorm_min (2^-1074), an explicit std::underflow_error is thrown before
 * final rounding, while exact denorm_min and representable subnormals are returned accurately.
 *
 * @param field The input complex field.
 * @return ScalarField2D Pointwise linear intensity with matching dimensions and physical metadata.
 * @throws std::invalid_argument If any sample in the field contains NaN or Inf.
 * @throws std::overflow_error If intensity calculation overflows double precision.
 * @throws std::underflow_error If non-zero sample intensity is strictly less than double precision denorm_min.
 */
[[nodiscard]] ScalarField2D computeIntensity(const ComplexField2D& field);

/**
 * @brief Computes decibel (dB) log intensity I_dB(x, y) = max(20 * log10(|U(x, y)|) - 10 * log10(referenceIntensity), floorDecibels).
 *
 * The output represents logarithmic intensity in decibels (dB) relative to referenceIntensity,
 * clamped from below by floorDecibels (dB <= 0). Evaluated via stable amplitude logarithm:
 * 20 * log10(|U|) - 10 * log10(referenceIntensity) without squaring first, preventing premature
 * intermediate underflow/overflow across extreme amplitudes (including subnormals).
 * Exact zero amplitude (0 + 0i) is directly assigned floorDecibels.
 *
 * @param field The input complex field.
 * @param floorDecibels Finite non-positive lower bound in decibels (must be <= 0.0).
 * @param referenceIntensity Strictly positive finite reference intensity level (default 1.0).
 * @return ScalarField2D Pointwise decibel intensity with matching dimensions and physical metadata.
 * @throws std::invalid_argument If floorDecibels is positive or non-finite, if referenceIntensity is non-positive
 *                               or non-finite, or if any sample is non-finite.
 * @throws std::overflow_error If decibel conversion overflows double precision.
 */
[[nodiscard]] ScalarField2D computeDecibelIntensity(
    const ComplexField2D& field,
    double floorDecibels = -120.0,
    double referenceIntensity = 1.0);

/**
 * @brief Computes principal wrapped phase phi(x, y) = atan2(Im(U(x, y)), Re(U(x, y))) in radians on [-pi, +pi).
 *
 * All angles are normalized to the unique half-open principal interval [-pi, +pi).
 * Points on the negative real axis (whether imag is +0.0 or -0.0) are uniformly mapped to -pi rad.
 *
 * For minimumIntensity == 0.0, only exact zero amplitude (0 + 0i) has undefined phase; all non-zero
 * finite samples (including subnormals) produce valid phase angles.
 * For minimumIntensity > 0.0, amplitude magnitude is stably compared against sqrt(minimumIntensity)
 * using std::hypot without squaring first to avoid premature underflow. Samples with magnitude >= sqrt(minimumIntensity)
 * are valid (validityMask == 1), while samples with magnitude < sqrt(minimumIntensity) are invalid (validityMask == 0).
 * For undefined/sub-threshold samples, the phase is deterministically set to 0.0 rad.
 *
 * @param field The input complex field.
 * @param minimumIntensity Non-negative finite intensity threshold below which phase is considered undefined.
 *                         Exact zero (0 + 0i) is always invalid regardless of threshold.
 * @return PhaseResult Struct containing wrappedPhaseRadians (ScalarField2D) and sample validityMask.
 * @throws std::invalid_argument If minimumIntensity is negative or non-finite, or if any sample is non-finite.
 */
[[nodiscard]] PhaseResult computeWrappedPhase(
    const ComplexField2D& field,
    double minimumIntensity = 0.0);

/**
 * @brief Computes discrete integrated relative intensity across the transverse plane: sum(|U(x, y)|^2) * dx * dy.
 *
 * Evaluated via scaled sum-of-squares and base-2 exponent decomposition to avoid intermediate underflow
 * or overflow during single-sample squaring or area product (dx * dy). Balanced extreme scale fields
 * (e.g. huge amplitude with tiny pitch, or tiny amplitude with huge pitch) whose mathematical result is
 * representable in double precision evaluate accurately.
 * Exact zero fields evaluate to 0.0. If the mathematical non-zero integrated intensity is strictly less than
 * double precision denorm_min (2^-1074), an explicit std::underflow_error is thrown before final rounding,
 * while exact denorm_min and representable subnormals are returned accurately.
 *
 * Units are [field-amplitude-squared * m^2].
 * Conversion to absolute radiometric power (Watts) requires calibrated optical impedance and source normalization.
 *
 * @param field The input complex field.
 * @return double Total integrated relative intensity across the grid.
 * @throws std::invalid_argument If any complex sample in the field contains NaN or Inf.
 * @throws std::overflow_error If integrated intensity calculation overflows double precision.
 * @throws std::underflow_error If non-zero integrated intensity is strictly less than double precision denorm_min.
 */
[[nodiscard]] double computeIntegratedIntensity(const ComplexField2D& field);

/**
 * @brief Computes discrete integrated relative intensity from an existing linear intensity field: sum(I(x, y)) * dx * dy.
 *
 * Evaluated via scaled sum and base-2 exponent decomposition to avoid premature area product underflow/overflow.
 * Exact zero fields evaluate to 0.0. If non-zero total integrated intensity is strictly less than
 * double precision denorm_min (2^-1074), an explicit std::underflow_error is thrown before final rounding,
 * while exact denorm_min and representable subnormals are returned accurately.
 *
 * Units are [field-amplitude-squared * m^2].
 * Conversion to absolute radiometric power (Watts) requires calibrated optical impedance and source normalization.
 *
 * @param intensityField The input linear intensity field.
 * @return double Total integrated relative intensity across the grid.
 * @throws std::invalid_argument If any intensity sample is negative, NaN, or Inf.
 * @throws std::overflow_error If integrated intensity calculation overflows double precision.
 * @throws std::underflow_error If non-zero integrated intensity is strictly less than double precision denorm_min.
 */
[[nodiscard]] double computeIntegratedIntensity(const ScalarField2D& intensityField);

} // namespace holobench::field
