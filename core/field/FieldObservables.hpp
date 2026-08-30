#pragma once

#include <cstdint>
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
struct PhaseResult final {
    ScalarField2D wrappedPhaseRadians;
    std::vector<uint8_t> validityMask; ///< 1 for valid defined phase, 0 for undefined/sub-threshold.

    [[nodiscard]] bool isValid(std::size_t xIndex, std::size_t yIndex) const {
        if (xIndex >= wrappedPhaseRadians.width() || yIndex >= wrappedPhaseRadians.height()) {
            throw std::out_of_range("phase result sample index out of range");
        }
        return validityMask[yIndex * wrappedPhaseRadians.width() + xIndex] != 0;
    }

    [[nodiscard]] bool isValid(std::size_t flatIndex) const {
        if (flatIndex >= validityMask.size()) {
            throw std::out_of_range("phase result sample index out of range");
        }
        return validityMask[flatIndex] != 0;
    }
};

/**
 * @brief Computes linear intensity I(x, y) = |U(x, y)|^2 = Re(U)^2 + Im(U)^2 for every sample.
 *
 * @param field The input complex field.
 * @return ScalarField2D Pointwise linear intensity with matching dimensions and physical metadata.
 * @throws std::invalid_argument If any sample in the field contains NaN or Inf.
 * @throws std::overflow_error If intensity calculation overflows double precision.
 */
[[nodiscard]] ScalarField2D computeIntensity(const ComplexField2D& field);

/**
 * @brief Computes natural log intensity I_ln(x, y) = ln(max(|U(x, y)|^2, minimumIntensity)).
 *
 * Pointwise natural logarithm of linear intensity clamped from below by minimumIntensity.
 *
 * @param field The input complex field.
 * @param minimumIntensity Strictly positive finite lower bound threshold for intensity.
 * @return ScalarField2D Pointwise natural log intensity with matching dimensions and physical metadata.
 * @throws std::invalid_argument If minimumIntensity is non-positive or non-finite, or if any field sample is non-finite.
 * @throws std::overflow_error If intensity calculation overflows double precision.
 */
[[nodiscard]] ScalarField2D computeNaturalLogIntensity(
    const ComplexField2D& field,
    double minimumIntensity);

/**
 * @brief Computes decibel (dB) log intensity I_dB(x, y) = max(10 * log10(|U(x, y)|^2 / referenceIntensity), floorDecibels).
 *
 * The output represents logarithmic intensity in decibels (dB) relative to referenceIntensity,
 * clamped from below by floorDecibels (dB <= 0). Evaluated via stable log differences:
 * 10 * (log10(I) - log10(referenceIntensity)) to prevent intermediate quotient underflow/overflow.
 * Exact zero intensity is directly assigned floorDecibels without division.
 *
 * @param field The input complex field.
 * @param floorDecibels Finite non-positive lower bound in decibels (must be <= 0.0).
 * @param referenceIntensity Strictly positive finite reference intensity level (default 1.0).
 * @return ScalarField2D Pointwise decibel intensity with matching dimensions and physical metadata.
 * @throws std::invalid_argument If floorDecibels is positive or non-finite, if referenceIntensity is non-positive
 *                               or non-finite, or if any sample is non-finite.
 * @throws std::overflow_error If intensity calculation or decibel conversion overflows double precision.
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
 * Physical phase is undefined for exact zero amplitude (0 + 0i) or samples where intensity < minimumIntensity.
 * For undefined/sub-threshold samples, the returned validityMask entry is set to 0 and the phase is
 * deterministically set to 0.0 rad. Only samples with validityMask == 1 represent valid optical phases.
 *
 * @param field The input complex field.
 * @param minimumIntensity Non-negative finite intensity threshold below which phase is considered undefined.
 *                         Exact zero (0 + 0i) is always invalid regardless of threshold.
 * @return PhaseResult Struct containing wrappedPhaseRadians (ScalarField2D) and sample validityMask.
 * @throws std::invalid_argument If minimumIntensity is negative or non-finite, or if any sample is non-finite.
 * @throws std::overflow_error If intermediate intensity calculation overflows double precision.
 */
[[nodiscard]] PhaseResult computeWrappedPhase(
    const ComplexField2D& field,
    double minimumIntensity = 0.0);

/**
 * @brief Computes discrete integrated intensity across the transverse plane: P = sum(|U(x, y)|^2) * dx * dy.
 *
 * Represents the discrete transverse plane integral of field intensity.
 * Units are [field-amplitude-squared * m^2].
 * Conversion to absolute radiometric power (Watts) requires calibrated optical impedance and source normalization.
 *
 * @param field The input complex field.
 * @return double Total integrated intensity across the grid.
 * @throws std::invalid_argument If any complex sample in the field contains NaN or Inf.
 * @throws std::overflow_error If intermediate summation or area scaling overflows double precision.
 */
[[nodiscard]] double computeIntegratedIntensity(const ComplexField2D& field);

/**
 * @brief Computes discrete integrated intensity from an existing linear intensity field: P = sum(I(x, y)) * dx * dy.
 *
 * Units are [field-amplitude-squared * m^2].
 * Conversion to absolute radiometric power (Watts) requires calibrated optical impedance and source normalization.
 *
 * @param intensityField The input linear intensity field.
 * @return double Total integrated intensity across the grid.
 * @throws std::invalid_argument If any intensity sample is negative, NaN, or Inf.
 * @throws std::overflow_error If intermediate summation or area scaling overflows double precision.
 */
[[nodiscard]] double computeIntegratedIntensity(const ScalarField2D& intensityField);

} // namespace holobench::field
