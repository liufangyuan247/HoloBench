#include "core/field/ComplexField2D.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>

namespace holobench::field {
namespace {

void requirePositiveFinite(double value, const char* name) {
    if (!std::isfinite(value) || value <= 0.0) {
        throw std::invalid_argument(std::string(name) + " must be positive and finite");
    }
}

} // namespace

ComplexField2D::ComplexField2D(
    std::size_t width,
    std::size_t height,
    double pitchXMetres,
    double pitchYMetres,
    double vacuumWavelengthMetres,
    double refractiveIndex)
    : width_(width)
    , height_(height)
    , pitchXMetres_(pitchXMetres)
    , pitchYMetres_(pitchYMetres)
    , vacuumWavelengthMetres_(vacuumWavelengthMetres)
    , refractiveIndex_(refractiveIndex)
    , samples_(checkedSampleCount(
          width,
          height,
          pitchXMetres,
          pitchYMetres,
          vacuumWavelengthMetres,
          refractiveIndex)) {
}

double ComplexField2D::mediumWavenumberRadiansPerMetre() const noexcept {
    return 2.0 * std::numbers::pi * refractiveIndex_ / vacuumWavelengthMetres_;
}

double ComplexField2D::xCoordinateMetres(std::size_t xIndex) const {
    if (xIndex >= width_) {
        throw std::out_of_range("complex field x index out of range");
    }
    const auto centerIndex = width_ / 2;
    return (static_cast<double>(xIndex) - static_cast<double>(centerIndex)) * pitchXMetres_;
}

double ComplexField2D::yCoordinateMetres(std::size_t yIndex) const {
    if (yIndex >= height_) {
        throw std::out_of_range("complex field y index out of range");
    }
    const auto centerIndex = height_ / 2;
    return (static_cast<double>(yIndex) - static_cast<double>(centerIndex)) * pitchYMetres_;
}

ComplexField2D::Sample& ComplexField2D::at(std::size_t xIndex, std::size_t yIndex) {
    return samples_.at(flatIndex(xIndex, yIndex));
}

const ComplexField2D::Sample& ComplexField2D::at(std::size_t xIndex, std::size_t yIndex) const {
    return samples_.at(flatIndex(xIndex, yIndex));
}

void ComplexField2D::fill(Sample value) noexcept {
    std::fill(samples_.begin(), samples_.end(), value);
}

std::size_t ComplexField2D::checkedSampleCount(
    std::size_t width,
    std::size_t height,
    double pitchXMetres,
    double pitchYMetres,
    double vacuumWavelengthMetres,
    double refractiveIndex) {
    if (width == 0 || height == 0) {
        throw std::invalid_argument("complex field dimensions must be nonzero");
    }
    requirePositiveFinite(pitchXMetres, "complex field x pitch");
    requirePositiveFinite(pitchYMetres, "complex field y pitch");
    requirePositiveFinite(vacuumWavelengthMetres, "complex field vacuum wavelength");
    requirePositiveFinite(refractiveIndex, "complex field refractive index");

    if (width > std::numeric_limits<std::size_t>::max() / height) {
        throw std::overflow_error("complex field sample count overflows size_t");
    }
    const auto count = width * height;
    if (count > std::vector<Sample>().max_size()) {
        throw std::length_error("complex field sample count exceeds vector max_size");
    }
    return count;
}

std::size_t ComplexField2D::flatIndex(std::size_t xIndex, std::size_t yIndex) const {
    if (xIndex >= width_ || yIndex >= height_) {
        throw std::out_of_range("complex field sample index out of range");
    }
    return yIndex * width_ + xIndex;
}

} // namespace holobench::field
