#pragma once

#include "core/math/Vec3.hpp"

namespace holobench::optics::ray {

struct Ray final {
    math::Vec3d originMetres;
    math::Vec3d direction;
    double wavelengthMetres = 532e-9;
    double power = 1.0;
};

[[nodiscard]] Ray makeRay(
    math::Vec3d originMetres,
    math::Vec3d direction,
    double wavelengthMetres = 532e-9,
    double power = 1.0);

} // namespace holobench::optics::ray

