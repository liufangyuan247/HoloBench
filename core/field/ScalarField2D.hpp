#pragma once

#include <cstddef>
#include <span>
#include <vector>

namespace holobench::field {

class ComplexField2D;

class ScalarField2D final {
public:
    ScalarField2D(
        std::size_t width,
        std::size_t height,
        double pitchXMetres,
        double pitchYMetres,
        double vacuumWavelengthMetres = 0.0,
        double refractiveIndex = 1.0);

    [[nodiscard]] static ScalarField2D createMatching(const ComplexField2D& source);

    [[nodiscard]] std::size_t width() const noexcept { return width_; }
    [[nodiscard]] std::size_t height() const noexcept { return height_; }
    [[nodiscard]] std::size_t sampleCount() const noexcept { return samples_.size(); }
    [[nodiscard]] double pitchXMetres() const noexcept { return pitchXMetres_; }
    [[nodiscard]] double pitchYMetres() const noexcept { return pitchYMetres_; }
    [[nodiscard]] double vacuumWavelengthMetres() const noexcept { return vacuumWavelengthMetres_; }
    [[nodiscard]] double refractiveIndex() const noexcept { return refractiveIndex_; }

    [[nodiscard]] double xCoordinateMetres(std::size_t xIndex) const;
    [[nodiscard]] double yCoordinateMetres(std::size_t yIndex) const;

    [[nodiscard]] double& at(std::size_t xIndex, std::size_t yIndex);
    [[nodiscard]] const double& at(std::size_t xIndex, std::size_t yIndex) const;

    [[nodiscard]] std::span<double> samples() noexcept { return samples_; }
    [[nodiscard]] std::span<const double> samples() const noexcept { return samples_; }
    void fill(double value) noexcept;

private:
    [[nodiscard]] static std::size_t checkedSampleCount(
        std::size_t width,
        std::size_t height,
        double pitchXMetres,
        double pitchYMetres,
        double vacuumWavelengthMetres,
        double refractiveIndex);
    [[nodiscard]] std::size_t flatIndex(std::size_t xIndex, std::size_t yIndex) const;

    std::size_t width_;
    std::size_t height_;
    double pitchXMetres_;
    double pitchYMetres_;
    double vacuumWavelengthMetres_;
    double refractiveIndex_;
    std::vector<double> samples_;
};

} // namespace holobench::field
