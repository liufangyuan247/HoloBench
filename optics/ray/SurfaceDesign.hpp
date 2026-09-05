#pragma once

#include "optics/ray/RotationalSurface.hpp"
#include <cmath>
#include <stdexcept>

namespace holobench::optics::ray {
// Invert the single-valued conic sag at the clear edge, preserving k and the
// even-asphere terms. R^2 = 2*s/c - (1+k)*s^2; lengths remain metres.
[[nodiscard]] inline RotationalSurface surfaceWithEdgeSag(const RotationalSurface &original,
                                                          double desiredSagMetres) {
    validateRotationalSurface(original);
    if (!std::isfinite(desiredSagMetres))
        throw std::invalid_argument("Sag must be finite");
    auto surface = original;
    double sag = desiredSagMetres;
    for (const auto &term : surface.evenAsphereTerms)
        sag -= term.coefficientSi * std::pow(surface.clearSemiDiameterMetres, term.radialOrder);
    const double denominator = surface.clearSemiDiameterMetres * surface.clearSemiDiameterMetres +
                               (1.0 + surface.conicConstant) * sag * sag;
    if (denominator <= 0.0)
        throw std::invalid_argument("Sag is outside the conic branch");
    surface.curvaturePerMetre = 2.0 * sag / denominator;
    validateRotationalSurface(surface);
    if (std::abs(evaluateSurfaceSag(surface, surface.clearSemiDiameterMetres).sagMetres -
                 desiredSagMetres) > 1e-10)
        throw std::invalid_argument("Sag is outside the single-valued conic branch");
    return surface;
}
} // namespace holobench::optics::ray
