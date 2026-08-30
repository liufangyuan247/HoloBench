#include "core/field/ScalarField2D.hpp"
#include "core/field/ComplexField2D.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

namespace holobench::field {
namespace {

void requirePositiveFinite(double value, const char* name) {
    if (!std::isfinite(value) || value <= 0.0) {
        throw std::invalid_argument(std::string(name) + " must be positive and finite");
    }
}

void requireNonNegativeFinite(double value, const char* name) {
    if (!std::isfinite(value) || value < 0.0) {
        throw std::invalid_argument(std::string(name) + " must be non-negative and finite");
    }
}

} // namespace

ScalarField2D::ScalarField2D(
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

ScalarField2D ScalarField2D::createMatching(const ComplexField2D& source) {
    return ScalarField2D(
        source.width(),
        source.height(),
        source.pitchXMetres(),
        source.pitchYMetres(),
        source.vacuumWavelengthMetres(),
        source.refractiveIndex());
}

double ScalarField2D::xCoordinateMetres(std::size_t xIndex) const {
    if (xIndex >= width_) {
        throw std::out_of_range("scalar field x index out of range");
    }
    const auto centerIndex = width_ / 2;
    return (static_cast<double>(xIndex) - static_cast<double>(centerIndex)) * pitchXMetres_;
}

double ScalarField2D::yCoordinateMetres(std::size_t yIndex) const {
    if (yIndex >= height_) {
        throw std::out_of_range("scalar field y index out of range");
    }
    const auto centerIndex = height_ / 2;
    return (static_cast<double>(yIndex) - static_cast<double>(centerIndex)) * pitchYMetres_;
}

double& ScalarField2D::at(std::size_t xIndex, std::size_t yIndex) {
    return samples_.at(flatIndex(xIndex, yIndex));
}

const double& ScalarField2D::at(std::size_t xIndex, std::size_t yIndex) const {
    return samples_.at(flatIndex(xIndex, yIndex));
}

void ScalarField2D::fill(double value) noexcept {
    std::fill(samples_.begin(), samples_.end(), value);
}

std::size_t ScalarField2D::checkedSampleCount(
    std::size_t width,
    std::size_t height,
    double pitchXMetres,
    double pitchYMetres,
    double vacuumWavelengthMetres,
    double refractiveIndex) {
    if (width == 0 || height == 0) {
        throw std::invalid_argument("scalar field dimensions must be nonzero");
    }
    requirePositiveFinite(pitchXMetres, "scalar field x pitch");
    requirePositiveFinite(pitchYMetres, "scalar field y pitch");
    requireNonNegativeFinite(vacuumWavelengthMetres, "scalar field vacuum wavelength");
    requirePositiveFinite(refractiveIndex, "scalar field refractive index");

    if (width > std::numeric_limits<std::size_t>::max() / height) {
        throw std::overflow_error("scalar field sample count overflows size_t");
    }
    const auto count = width * height;
    if (count > std::vector<double>().max_size()) {
        throw std::length_error("scalar field sample count exceeds vector max_size");
    }
    return count;
}

std::size_t ScalarField2D::flatIndex(std::size_t xIndex, std::size_t yIndex) const {
    if (xIndex >= width_ || yIndex >= height_) {
        throw std::out_of_range("scalar field sample index out of range");
    }
    return yIndex * width_ + xIndex;
}

} // namespace holobench::field
