#pragma once

#include <cmath>
#include <stdexcept>

namespace holobench::math {

struct Vec3d final {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    constexpr bool operator==(const Vec3d&) const noexcept = default;
};

[[nodiscard]] constexpr Vec3d operator+(Vec3d lhs, Vec3d rhs) noexcept {
    return {lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z};
}

[[nodiscard]] constexpr Vec3d operator-(Vec3d lhs, Vec3d rhs) noexcept {
    return {lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
}

[[nodiscard]] constexpr Vec3d operator-(Vec3d value) noexcept {
    return {-value.x, -value.y, -value.z};
}

[[nodiscard]] constexpr Vec3d operator*(Vec3d value, double scale) noexcept {
    return {value.x * scale, value.y * scale, value.z * scale};
}

[[nodiscard]] constexpr Vec3d operator*(double scale, Vec3d value) noexcept {
    return value * scale;
}

[[nodiscard]] constexpr Vec3d operator/(Vec3d value, double divisor) noexcept {
    return {value.x / divisor, value.y / divisor, value.z / divisor};
}

[[nodiscard]] constexpr double dot(Vec3d lhs, Vec3d rhs) noexcept {
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

[[nodiscard]] constexpr Vec3d cross(Vec3d lhs, Vec3d rhs) noexcept {
    return {
        lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.z * rhs.x - lhs.x * rhs.z,
        lhs.x * rhs.y - lhs.y * rhs.x,
    };
}

[[nodiscard]] constexpr double lengthSquared(Vec3d value) noexcept {
    return dot(value, value);
}

[[nodiscard]] inline double length(Vec3d value) noexcept {
    return std::sqrt(lengthSquared(value));
}

[[nodiscard]] inline bool isFinite(Vec3d value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] inline Vec3d normalized(Vec3d value) {
    const double magnitude = length(value);
    if (!std::isfinite(magnitude) || magnitude <= 0.0) {
        throw std::invalid_argument("cannot normalize a zero or non-finite vector");
    }
    return value / magnitude;
}

} // namespace holobench::math

