#include "optics/ray/Ray.hpp"

#include <cmath>
#include <stdexcept>

namespace holobench::optics::ray {

Ray makeRay(math::Vec3d originMetres, math::Vec3d direction, double wavelengthMetres, double power) {
    if (!math::isFinite(originMetres)) {
        throw std::invalid_argument("ray origin must be finite");
    }
    if (!std::isfinite(wavelengthMetres) || wavelengthMetres <= 0.0) {
        throw std::invalid_argument("ray wavelength must be finite and positive");
    }
    if (!std::isfinite(power) || power < 0.0) {
        throw std::invalid_argument("ray power must be finite and non-negative");
    }
    return {originMetres, math::normalized(direction), wavelengthMetres, power};
}

} // namespace holobench::optics::ray

