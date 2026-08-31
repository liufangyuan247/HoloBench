#include "core/field/RgbFieldVisualization.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace holobench::field {
namespace {

void requireSharedGrid(
    const ComplexField2D& red,
    const ComplexField2D& value) {
    if (value.width() != red.width()
        || value.height() != red.height()
        || value.pitchXMetres() != red.pitchXMetres()
        || value.pitchYMetres() != red.pitchYMetres()) {
        throw std::invalid_argument(
            "RGB intensity display fields must share one transverse sampling grid");
    }
}

std::uint8_t encode(double value, double gamma) noexcept {
    const double encoded = std::pow(std::clamp(value, 0.0, 1.0), 1.0 / gamma);
    return static_cast<std::uint8_t>(std::round(encoded * 255.0));
}

} // namespace

RgbaImage renderUncalibratedRgbIntensity(
    const ComplexField2D& red,
    const ComplexField2D& green,
    const ComplexField2D& blue,
    const RgbIntensityVisualizationOptions& options) {
    requireSharedGrid(red, green);
    requireSharedGrid(red, blue);
    bool anyPositiveGain = false;
    for (const double gain : options.channelIntensityGains) {
        if (!std::isfinite(gain) || gain < 0.0) {
            throw std::invalid_argument(
                "RGB display intensity gains must be finite and non-negative");
        }
        anyPositiveGain = anyPositiveGain || gain > 0.0;
    }
    if (!anyPositiveGain
        || !std::isfinite(options.referenceIntensity)
        || options.referenceIntensity < 0.0
        || !std::isfinite(options.displayGamma)
        || options.displayGamma <= 0.0) {
        throw std::invalid_argument(
            "RGB display requires a positive gain, non-negative finite reference, and positive finite gamma");
    }

    const std::array<ScalarField2D, 3> intensities {
        computeIntensity(red),
        computeIntensity(green),
        computeIntensity(blue),
    };
    double reference = options.referenceIntensity;
    if (reference == 0.0) {
        for (std::size_t index = 0; index < red.sampleCount(); ++index) {
            for (std::size_t channel = 0; channel < 3U; ++channel) {
                reference = std::max(
                    reference,
                    intensities[channel].samples()[index]
                        * options.channelIntensityGains[channel]);
            }
        }
        if (reference == 0.0) {
            reference = 1.0;
        }
    }
    if (!std::isfinite(reference) || reference <= 0.0) {
        throw std::overflow_error(
            "RGB display reference intensity is not representable");
    }
    if (red.sampleCount()
        > std::numeric_limits<std::size_t>::max() / 4U) {
        throw std::overflow_error("RGB display byte count overflows size_t");
    }
    std::vector<std::uint8_t> pixels(red.sampleCount() * 4U, 0U);
    for (std::size_t index = 0; index < red.sampleCount(); ++index) {
        for (std::size_t channel = 0; channel < 3U; ++channel) {
            const double weighted = intensities[channel].samples()[index]
                * options.channelIntensityGains[channel];
            if (!std::isfinite(weighted)) {
                throw std::overflow_error(
                    "RGB weighted display intensity is not representable");
            }
            pixels[index * 4U + channel]
                = encode(weighted / reference, options.displayGamma);
        }
        pixels[index * 4U + 3U] = 255U;
    }
    return RgbaImage(red.width(), red.height(), std::move(pixels));
}

} // namespace holobench::field
