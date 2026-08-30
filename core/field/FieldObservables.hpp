#pragma once

#include "core/field/ComplexField2D.hpp"
#include "core/field/ScalarField2D.hpp"

namespace holobench::field {

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
 * @brief Computes natural log intensity I_ln(x, y) = ln(max(|U(x, y)|^2, floorValue)).
 *
 * The output represents natural logarithm (base e, nepers) of intensity clamped from below by floorValue.
 *
 * @param field The input complex field.
 * @param floorValue Strictly positive finite lower bound threshold for intensity.
 * @return ScalarField2D Pointwise natural log intensity with matching dimensions and physical metadata.
 * @throws std::invalid_argument If floorValue is non-positive or non-finite, or if any field sample is non-finite.
 * @throws std::overflow_error If intensity calculation overflows double precision.
 */
[[nodiscard]] ScalarField2D computeNaturalLogIntensity(const ComplexField2D& field, double floorValue);

/**
 * @brief Computes decibel (dB) log intensity I_dB(x, y) = 10 * log10(max(|U(x, y)|^2, floorValue) / referenceIntensity).
 *
 * The output represents logarithmic power/intensity in decibels (dB) relative to referenceIntensity,
 * clamped from below by floorValue.
 *
 * @param field The input complex field.
 * @param floorValue Strictly positive finite lower bound threshold for intensity.
 * @param referenceIntensity Strictly positive finite reference intensity level (default 1.0).
 * @return ScalarField2D Pointwise decibel intensity with matching dimensions and physical metadata.
 * @throws std::invalid_argument If floorValue or referenceIntensity is non-positive or non-finite, or if any sample is non-finite.
 * @throws std::overflow_error If intensity calculation or decibel conversion overflows double precision.
 */
[[nodiscard]] ScalarField2D computeDecibelIntensity(
    const ComplexField2D& field,
    double floorValue,
    double referenceIntensity = 1.0);

/**
 * @brief Computes principal wrapped phase phi(x, y) = atan2(Im(U(x, y)), Re(U(x, y))) in radians.
 *
 * The returned values lie in the principal interval [-pi, +pi].
 * Follows standard IEEE 754 branch cut along the negative real axis.
 * Exact zero (0 + 0i) yields 0.0 rad.
 *
 * @param field The input complex field.
 * @return ScalarField2D Pointwise wrapped phase in radians with matching dimensions and physical metadata.
 * @throws std::invalid_argument If any sample in the field contains NaN or Inf.
 */
[[nodiscard]] ScalarField2D computeWrappedPhase(const ComplexField2D& field);

/**
 * @brief Computes discrete integrated power across the transverse plane: P = sum(|U(x, y)|^2) * dx * dy.
 *
 * Represents the discrete transverse plane integral of intensity (SI unit: intensity * m^2).
 *
 * @param field The input complex field.
 * @return double Total integrated power.
 * @throws std::invalid_argument If any complex sample in the field contains NaN or Inf.
 * @throws std::overflow_error If intermediate summation or area scaling overflows double precision.
 */
[[nodiscard]] double computeIntegratedPower(const ComplexField2D& field);

/**
 * @brief Computes discrete integrated power from an existing linear intensity field: P = sum(I(x, y)) * dx * dy.
 *
 * @param intensityField The input linear intensity field.
 * @return double Total integrated power.
 * @throws std::invalid_argument If any intensity sample is negative, NaN, or Inf.
 * @throws std::overflow_error If intermediate summation or area scaling overflows double precision.
 */
[[nodiscard]] double computeIntegratedPower(const ScalarField2D& intensityField);

} // namespace holobench::field
