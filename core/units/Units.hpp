#pragma once

#include <compare>

namespace holobench::units {

class Length final {
public:
    [[nodiscard]] static constexpr Length fromMetres(double value) noexcept { return Length(value); }
    [[nodiscard]] static constexpr Length fromMillimetres(double value) noexcept { return Length(value / 1'000.0); }
    [[nodiscard]] static constexpr Length fromMicrometres(double value) noexcept { return Length(value / 1'000'000.0); }
    [[nodiscard]] static constexpr Length fromNanometres(double value) noexcept { return Length(value / 1'000'000'000.0); }

    [[nodiscard]] constexpr double metres() const noexcept { return metres_; }
    [[nodiscard]] constexpr double millimetres() const noexcept { return metres_ * 1'000.0; }
    [[nodiscard]] constexpr double micrometres() const noexcept { return metres_ * 1'000'000.0; }
    [[nodiscard]] constexpr double nanometres() const noexcept { return metres_ * 1'000'000'000.0; }

    [[nodiscard]] friend constexpr Length operator+(Length lhs, Length rhs) noexcept {
        return Length(lhs.metres_ + rhs.metres_);
    }
    [[nodiscard]] friend constexpr Length operator-(Length lhs, Length rhs) noexcept {
        return Length(lhs.metres_ - rhs.metres_);
    }
    [[nodiscard]] friend constexpr Length operator*(Length length, double scale) noexcept {
        return Length(length.metres_ * scale);
    }
    [[nodiscard]] friend constexpr Length operator*(double scale, Length length) noexcept {
        return length * scale;
    }
    [[nodiscard]] friend constexpr Length operator/(Length length, double divisor) noexcept {
        return Length(length.metres_ / divisor);
    }
    [[nodiscard]] friend constexpr double operator/(Length numerator, Length denominator) noexcept {
        return numerator.metres_ / denominator.metres_;
    }

    constexpr auto operator<=>(const Length&) const noexcept = default;

private:
    explicit constexpr Length(double metres) noexcept : metres_(metres) {}
    double metres_ = 0.0;
};

} // namespace holobench::units
