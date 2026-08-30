#include "compute/fft/CpuFftBackend.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

#include "core/field/ComplexField2D.hpp"

namespace holobench::compute::fft {
namespace {

[[nodiscard]] bool isPowerOfTwo(std::size_t value) noexcept {
    return value != 0 && (value & (value - 1)) == 0;
}

void validateFiniteSamples(const field::ComplexField2D& field) {
    for (const auto& sample : field.samples()) {
        if (!std::isfinite(sample.real()) || !std::isfinite(sample.imag())) {
            throw std::invalid_argument("FFT input samples must be finite");
        }
    }
}

} // namespace

bool CpuFftBackend::supportsDimensions(std::size_t width, std::size_t height) const noexcept {
    return isPowerOfTwo(width) && isPowerOfTwo(height);
}

void CpuFftBackend::forward2D(field::ComplexField2D& field) {
    transform2D(field, false);
}

void CpuFftBackend::inverse2D(field::ComplexField2D& field) {
    transform2D(field, true);
}

void CpuFftBackend::transform2D(field::ComplexField2D& field, bool inverse) {
    const auto width = field.width();
    const auto height = field.height();
    if (!supportsDimensions(width, height)) {
        throw std::invalid_argument("CPU radix-2 FFT requires power-of-two width and height");
    }
    validateFiniteSamples(field);

    const auto requiredScratch = std::max(width, height);
    scratch_.resize(requiredScratch);

    auto samples = field.samples();
    for (std::size_t y = 0; y < height; ++y) {
        transform1D(samples.subspan(y * width, width), inverse);
    }

    for (std::size_t x = 0; x < width; ++x) {
        for (std::size_t y = 0; y < height; ++y) {
            scratch_[y] = samples[y * width + x];
        }
        transform1D(std::span<std::complex<double>>(scratch_.data(), height), inverse);
        for (std::size_t y = 0; y < height; ++y) {
            samples[y * width + x] = scratch_[y];
        }
    }
}

void CpuFftBackend::transform1D(std::span<std::complex<double>> values, bool inverse) noexcept {
    const auto count = values.size();
    if (count <= 1) {
        return;
    }

    for (std::size_t index = 1, reversed = 0; index < count; ++index) {
        std::size_t bit = count >> 1;
        while ((reversed & bit) != 0) {
            reversed ^= bit;
            bit >>= 1;
        }
        reversed ^= bit;
        if (index < reversed) {
            std::swap(values[index], values[reversed]);
        }
    }

    std::size_t length = 2;
    while (length <= count) {
        const double sign = inverse ? 1.0 : -1.0;
        const double angle = sign * 2.0 * std::numbers::pi / static_cast<double>(length);
        const std::complex<double> root(std::cos(angle), std::sin(angle));
        const auto halfLength = length / 2;

        for (std::size_t block = 0; block < count; block += length) {
            std::complex<double> factor(1.0, 0.0);
            for (std::size_t offset = 0; offset < halfLength; ++offset) {
                const auto even = values[block + offset];
                const auto odd = values[block + offset + halfLength] * factor;
                values[block + offset] = even + odd;
                values[block + offset + halfLength] = even - odd;
                factor *= root;
            }
        }

        if (length == count) {
            break;
        }
        length *= 2;
    }

    if (inverse) {
        const double scale = 1.0 / static_cast<double>(count);
        for (auto& value : values) {
            value *= scale;
        }
    }
}

} // namespace holobench::compute::fft
