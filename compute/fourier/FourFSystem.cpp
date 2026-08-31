#include "compute/fourier/FourFSystem.hpp"

#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>

#include "core/field/FieldObservables.hpp"

namespace holobench::compute::fourier {
namespace {

void requirePositiveFiniteRadius(double radiusMetres, const char* name) {
    if (!std::isfinite(radiusMetres) || radiusMetres <= 0.0) {
        throw std::invalid_argument(std::string(name) + " must be positive and finite");
    }
}

[[nodiscard]] FourierFilterDiagnostics applyFilter(
    field::ComplexField2D& plane,
    const CircularFourierFilter& filter) {
    FourierFilterDiagnostics diagnostics;
    diagnostics.kind = filter.kind();
    diagnostics.innerRadiusMetres = filter.innerRadiusMetres();
    diagnostics.outerRadiusMetres = filter.outerRadiusMetres();
    diagnostics.totalSampleCount = plane.sampleCount();
    diagnostics.inputIntegratedIntensity = field::computeIntegratedIntensity(plane);

    for (std::size_t y = 0; y < plane.height(); ++y) {
        const double yMetres = plane.yCoordinateMetres(y);
        for (std::size_t x = 0; x < plane.width(); ++x) {
            const double radiusMetres = std::hypot(plane.xCoordinateMetres(x), yMetres);
            if (filter.transmitsRadiusMetres(radiusMetres)) {
                ++diagnostics.transmittedSampleCount;
            } else {
                plane.at(x, y) = {0.0, 0.0};
                ++diagnostics.blockedSampleCount;
            }
        }
    }

    diagnostics.outputIntegratedIntensity = field::computeIntegratedIntensity(plane);
    if (diagnostics.inputIntegratedIntensity != 0.0) {
        diagnostics.integratedIntensityTransmission = diagnostics.outputIntegratedIntensity
            / diagnostics.inputIntegratedIntensity;
    }
    return diagnostics;
}

} // namespace

CircularFourierFilter::CircularFourierFilter(
    CircularFilterKind kind,
    double innerRadiusMetres,
    double outerRadiusMetres) noexcept
    : kind_(kind),
      innerRadiusMetres_(innerRadiusMetres),
      outerRadiusMetres_(outerRadiusMetres) {
}

CircularFourierFilter CircularFourierFilter::passAll() noexcept {
    return {CircularFilterKind::PassAll, 0.0, 0.0};
}

CircularFourierFilter CircularFourierFilter::lowPass(double cutoffRadiusMetres) {
    requirePositiveFiniteRadius(cutoffRadiusMetres, "Low-pass cutoff radius");
    return {CircularFilterKind::LowPass, 0.0, cutoffRadiusMetres};
}

CircularFourierFilter CircularFourierFilter::highPass(double cutoffRadiusMetres) {
    requirePositiveFiniteRadius(cutoffRadiusMetres, "High-pass cutoff radius");
    return {CircularFilterKind::HighPass, cutoffRadiusMetres, 0.0};
}

CircularFourierFilter CircularFourierFilter::bandPass(
    double innerRadiusMetres,
    double outerRadiusMetres) {
    if (!std::isfinite(innerRadiusMetres) || innerRadiusMetres < 0.0) {
        throw std::invalid_argument("Band-pass inner radius must be non-negative and finite");
    }
    requirePositiveFiniteRadius(outerRadiusMetres, "Band-pass outer radius");
    if (innerRadiusMetres >= outerRadiusMetres) {
        throw std::invalid_argument("Band-pass outer radius must exceed its inner radius");
    }
    return {CircularFilterKind::BandPass, innerRadiusMetres, outerRadiusMetres};
}

bool CircularFourierFilter::transmitsRadiusMetres(double radiusMetres) const noexcept {
    if (!std::isfinite(radiusMetres) || radiusMetres < 0.0) {
        return false;
    }
    switch (kind_) {
    case CircularFilterKind::PassAll:
        return true;
    case CircularFilterKind::LowPass:
        return radiusMetres <= outerRadiusMetres_;
    case CircularFilterKind::HighPass:
        return radiusMetres >= innerRadiusMetres_;
    case CircularFilterKind::BandPass:
        return radiusMetres >= innerRadiusMetres_ && radiusMetres <= outerRadiusMetres_;
    }
    return false;
}

FourFSystem::FourFSystem(fft::IFftBackend& fftBackend) noexcept
    : transform_(fftBackend) {
}

FourFResult FourFSystem::run(
    const field::ComplexField2D& objectPlane,
    double firstFocalLengthMetres,
    double secondFocalLengthMetres,
    const CircularFourierFilter& filter) const {
    auto first = transform_.transformFrontToBackFocalPlane(
        objectPlane, firstFocalLengthMetres);
    auto filteredPlane = first.field;
    auto filterDiagnostics = applyFilter(filteredPlane, filter);
    auto second = transform_.transformFrontToBackFocalPlane(
        filteredPlane, secondFocalLengthMetres);

    return {
        std::move(first.field),
        std::move(filteredPlane),
        std::move(second.field),
        first.diagnostics,
        second.diagnostics,
        filterDiagnostics};
}

} // namespace holobench::compute::fourier
