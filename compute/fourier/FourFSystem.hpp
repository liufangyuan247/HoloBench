#pragma once

#include <cstddef>

#include "compute/fourier/FourierOptics.hpp"

namespace holobench::compute::fft {
class IFftBackend;
}

namespace holobench::compute::fourier {

enum class CircularFilterKind {
    PassAll,
    LowPass,
    HighPass,
    BandPass,
};

/**
 * An ideal, centred, hard-edged circular amplitude filter in a physical
 * Fourier plane. Radius is measured in metres from the optical axis.
 * Boundary samples are transmitted.
 */
class CircularFourierFilter final {
public:
    [[nodiscard]] static CircularFourierFilter passAll() noexcept;
    [[nodiscard]] static CircularFourierFilter lowPass(double cutoffRadiusMetres);
    [[nodiscard]] static CircularFourierFilter highPass(double cutoffRadiusMetres);
    [[nodiscard]] static CircularFourierFilter bandPass(
        double innerRadiusMetres,
        double outerRadiusMetres);

    [[nodiscard]] CircularFilterKind kind() const noexcept { return kind_; }
    [[nodiscard]] double innerRadiusMetres() const noexcept { return innerRadiusMetres_; }
    [[nodiscard]] double outerRadiusMetres() const noexcept { return outerRadiusMetres_; }
    [[nodiscard]] bool transmitsRadiusMetres(double radiusMetres) const noexcept;

private:
    CircularFourierFilter(
        CircularFilterKind kind,
        double innerRadiusMetres,
        double outerRadiusMetres) noexcept;

    CircularFilterKind kind_;
    double innerRadiusMetres_;
    double outerRadiusMetres_;
};

struct FourierFilterDiagnostics final {
    CircularFilterKind kind = CircularFilterKind::PassAll;
    double innerRadiusMetres = 0.0;
    double outerRadiusMetres = 0.0;
    std::size_t totalSampleCount = 0U;
    std::size_t transmittedSampleCount = 0U;
    std::size_t blockedSampleCount = 0U;
    double inputIntegratedIntensity = 0.0;
    double outputIntegratedIntensity = 0.0;
    double integratedIntensityTransmission = 0.0;
};

struct FourFResult final {
    field::ComplexField2D fourierPlaneBeforeFilter;
    field::ComplexField2D fourierPlaneAfterFilter;
    field::ComplexField2D imagePlane;
    FourierPlaneDiagnostics firstTransformDiagnostics;
    FourierPlaneDiagnostics secondTransformDiagnostics;
    FourierFilterDiagnostics filterDiagnostics;
};

/**
 * Runs an ideal coherent scalar 4-f relay. The first transform creates the
 * physical Fourier plane, the selected hard-edged circular amplitude filter
 * is applied there, and the second transform creates the inverted image plane.
 */
class FourFSystem final {
public:
    explicit FourFSystem(fft::IFftBackend& fftBackend) noexcept;

    [[nodiscard]] FourFResult run(
        const field::ComplexField2D& objectPlane,
        double firstFocalLengthMetres,
        double secondFocalLengthMetres,
        const CircularFourierFilter& filter = CircularFourierFilter::passAll()) const;

private:
    FourierLensTransform transform_;
};

} // namespace holobench::compute::fourier
