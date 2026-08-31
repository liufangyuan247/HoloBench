#include <doctest/doctest.h>

#include <cmath>
#include <limits>
#include <stdexcept>

#include "core/math/RigidTransform.hpp"

namespace math = holobench::math;

TEST_CASE("rigid transform point and direction round trips preserve metric geometry") {
    const double angle = 0.37;
    const math::RigidTransform3d transform {
        .translationMetres = {0.013, -0.027, 0.091},
        .localXAxisInWorld = {std::cos(angle), 0.0, -std::sin(angle)},
        .localYAxisInWorld = {0.0, 1.0, 0.0},
        .localZAxisInWorld = {std::sin(angle), 0.0, std::cos(angle)},
    };
    CHECK_NOTHROW(math::validateRigidTransform(transform));

    const math::Vec3d localPoint {0.004, -0.002, 0.018};
    const math::Vec3d worldPoint = math::transformPointLocalToWorld(transform, localPoint);
    const math::Vec3d recoveredPoint = math::transformPointWorldToLocal(transform, worldPoint);
    CHECK(recoveredPoint.x == doctest::Approx(localPoint.x).epsilon(2e-14));
    CHECK(recoveredPoint.y == doctest::Approx(localPoint.y).epsilon(2e-14));
    CHECK(recoveredPoint.z == doctest::Approx(localPoint.z).epsilon(2e-14));

    const math::Vec3d localDirection = math::normalized({0.2, -0.1, 1.0});
    const math::Vec3d worldDirection = math::transformDirectionLocalToWorld(transform, localDirection);
    const math::Vec3d recoveredDirection = math::transformDirectionWorldToLocal(transform, worldDirection);
    CHECK(math::length(worldDirection) == doctest::Approx(1.0).epsilon(2e-14));
    CHECK(recoveredDirection.x == doctest::Approx(localDirection.x).epsilon(2e-14));
    CHECK(recoveredDirection.y == doctest::Approx(localDirection.y).epsilon(2e-14));
    CHECK(recoveredDirection.z == doctest::Approx(localDirection.z).epsilon(2e-14));
}

TEST_CASE("rigid transform rejects scale shear reflection and non-finite state") {
    math::RigidTransform3d transform {};
    transform.localXAxisInWorld = {2.0, 0.0, 0.0};
    CHECK_THROWS_AS(math::validateRigidTransform(transform), std::invalid_argument);

    transform = {};
    transform.localYAxisInWorld = {0.1, 1.0, 0.0};
    CHECK_THROWS_AS(math::validateRigidTransform(transform), std::invalid_argument);

    transform = {};
    transform.localZAxisInWorld = {0.0, 0.0, -1.0};
    CHECK_THROWS_AS(math::validateRigidTransform(transform), std::invalid_argument);

    transform = {};
    transform.translationMetres.x = std::numeric_limits<double>::infinity();
    CHECK_THROWS_AS(math::validateRigidTransform(transform), std::invalid_argument);
}
