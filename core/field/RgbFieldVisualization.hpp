#pragma once

#include <array>

#include "core/field/FieldVisualization.hpp"

namespace holobench::field {

struct RgbIntensityVisualizationOptions final {
    std::array<double, 3> channelIntensityGains {1.0, 1.0, 1.0};
    // Zero selects the peak weighted channel intensity across all pixels.
    double referenceIntensity = 0.0;
    // Display encoding only. A value of 1 is linear; 2.2 is a simple monitor
    // gamma and is not a calibrated spectral colour transform.
    double displayGamma = 2.2;
};

// Combines separately computed red, green, and blue intensities only for
// display. It never sums complex fields and therefore cannot create a
// cross-wavelength interference term. Output RGB values are explicitly
// uncalibrated device primaries rather than a colorimetric prediction.
[[nodiscard]] RgbaImage renderUncalibratedRgbIntensity(
    const ComplexField2D& red,
    const ComplexField2D& green,
    const ComplexField2D& blue,
    const RgbIntensityVisualizationOptions& options = {});

} // namespace holobench::field
