#include "optics/slm/SlmResponse.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <utility>

namespace holobench::optics::slm {
namespace {

void requireFinite(double value, const char* message) {
    if (!std::isfinite(value)) {
        throw std::invalid_argument(message);
    }
}

void validatePoint(const SlmResponsePoint& point) {
    requireFinite(point.normalizedCommand, "SLM LUT commands must be finite");
    requireFinite(point.amplitudeTransmission, "SLM LUT amplitudes must be finite");
    requireFinite(point.unwrappedPhaseDelayRadians, "SLM LUT phases must be finite");
    if (point.normalizedCommand < 0.0 || point.normalizedCommand > 1.0) {
        throw std::invalid_argument("SLM LUT commands must be in [0, 1]");
    }
    if (point.amplitudeTransmission < 0.0 || point.amplitudeTransmission > 1.0) {
        throw std::invalid_argument("SLM LUT amplitude transmission must be in [0, 1]");
    }
}

[[nodiscard]] EvaluatedSlmResponse evaluateCurve(
    const SlmWavelengthResponse& curve,
    double normalizedCommand) {
    const auto upper = std::lower_bound(
        curve.commandResponse.begin(),
        curve.commandResponse.end(),
        normalizedCommand,
        [](const SlmResponsePoint& point, double command) {
            return point.normalizedCommand < command;
        });
    if (upper == curve.commandResponse.begin()) {
        return {upper->amplitudeTransmission, upper->unwrappedPhaseDelayRadians};
    }
    if (upper == curve.commandResponse.end()) {
        const auto& last = curve.commandResponse.back();
        return {last.amplitudeTransmission, last.unwrappedPhaseDelayRadians};
    }
    if (upper->normalizedCommand == normalizedCommand) {
        return {upper->amplitudeTransmission, upper->unwrappedPhaseDelayRadians};
    }
    const auto& lower = *(upper - 1);
    const double fraction = (normalizedCommand - lower.normalizedCommand)
        / (upper->normalizedCommand - lower.normalizedCommand);
    return {
        .amplitudeTransmission = std::lerp(
            lower.amplitudeTransmission, upper->amplitudeTransmission, fraction),
        .unwrappedPhaseDelayRadians = std::lerp(
            lower.unwrappedPhaseDelayRadians, upper->unwrappedPhaseDelayRadians, fraction),
    };
}

void validateLcdParameters(const LcdTeachingParameters& parameters) {
    requireFinite(parameters.inputPolarizerAngleRadians, "LCD input polarizer angle must be finite");
    requireFinite(parameters.analyzerAngleRadians, "LCD analyzer angle must be finite");
    requireFinite(parameters.liquidCrystalFastAxisAngleRadians, "LCD fast-axis angle must be finite");
    requireFinite(parameters.zeroCommandRetardanceRadians, "LCD zero-command retardance must be finite");
    requireFinite(parameters.fullCommandRetardanceRadians, "LCD full-command retardance must be finite");
    double previousWavelength = 0.0;
    for (const auto& sample : parameters.spectralTransmission) {
        requireFinite(sample.vacuumWavelengthMetres, "LCD filter wavelength must be finite");
        requireFinite(sample.redAmplitude, "LCD red transmission must be finite");
        requireFinite(sample.greenAmplitude, "LCD green transmission must be finite");
        requireFinite(sample.blueAmplitude, "LCD blue transmission must be finite");
        if (sample.vacuumWavelengthMetres <= previousWavelength) {
            throw std::invalid_argument("LCD filter wavelengths must be positive and strictly increasing");
        }
        if (sample.redAmplitude < 0.0 || sample.redAmplitude > 1.0
            || sample.greenAmplitude < 0.0 || sample.greenAmplitude > 1.0
            || sample.blueAmplitude < 0.0 || sample.blueAmplitude > 1.0) {
            throw std::invalid_argument("LCD filter field-amplitude transmission must be in [0, 1]");
        }
        previousWavelength = sample.vacuumWavelengthMetres;
    }
    if (parameters.colorFilterPattern != LcdColorFilterPattern::Monochrome
        && parameters.spectralTransmission.empty()) {
        throw std::invalid_argument("RGB LCD teaching model needs spectral transmission samples");
    }
}

[[nodiscard]] double channelAmplitude(
    const LcdSpectralTransmission& value,
    LcdColorChannel channel) noexcept {
    switch (channel) {
    case LcdColorChannel::Red:
        return value.redAmplitude;
    case LcdColorChannel::Green:
        return value.greenAmplitude;
    case LcdColorChannel::Blue:
        return value.blueAmplitude;
    }
    return 0.0;
}

[[nodiscard]] double interpolateFilterAmplitude(
    const LcdTeachingParameters& parameters,
    LcdColorChannel channel,
    double wavelength) {
    if (parameters.colorFilterPattern == LcdColorFilterPattern::Monochrome) {
        return 1.0;
    }
    const auto& samples = parameters.spectralTransmission;
    if (wavelength < samples.front().vacuumWavelengthMetres
        || wavelength > samples.back().vacuumWavelengthMetres) {
        throw std::out_of_range("LCD wavelength is outside the filter calibration domain");
    }
    const auto upper = std::lower_bound(
        samples.begin(),
        samples.end(),
        wavelength,
        [](const LcdSpectralTransmission& sample, double requested) {
            return sample.vacuumWavelengthMetres < requested;
        });
    if (upper == samples.begin() || upper->vacuumWavelengthMetres == wavelength) {
        return channelAmplitude(*upper, channel);
    }
    const auto& lower = *(upper - 1);
    const double fraction = (wavelength - lower.vacuumWavelengthMetres)
        / (upper->vacuumWavelengthMetres - lower.vacuumWavelengthMetres);
    return std::lerp(
        channelAmplitude(lower, channel), channelAmplitude(*upper, channel), fraction);
}

} // namespace

std::complex<double> EvaluatedSlmResponse::complexTransfer() const {
    if (!std::isfinite(amplitudeTransmission)
        || amplitudeTransmission < 0.0 || amplitudeTransmission > 1.0
        || !std::isfinite(unwrappedPhaseDelayRadians)) {
        throw std::invalid_argument("evaluated SLM response is outside its physical scalar domain");
    }
    return amplitudeTransmission * std::polar(
        1.0, std::remainder(unwrappedPhaseDelayRadians, 2.0 * std::numbers::pi));
}

CalibratedSlmResponse::CalibratedSlmResponse(
    std::vector<SlmWavelengthResponse> wavelengths)
    : wavelengths_(std::move(wavelengths)) {
    if (wavelengths_.empty()) {
        throw std::invalid_argument("SLM response needs at least one wavelength curve");
    }
    double previousWavelength = 0.0;
    for (const auto& curve : wavelengths_) {
        requireFinite(curve.vacuumWavelengthMetres, "SLM LUT wavelengths must be finite");
        if (curve.vacuumWavelengthMetres <= previousWavelength) {
            throw std::invalid_argument("SLM LUT wavelengths must be positive and strictly increasing");
        }
        if (curve.commandResponse.size() < 2) {
            throw std::invalid_argument("SLM LUT curves need at least two command samples");
        }
        double previousCommand = -1.0;
        for (const auto& point : curve.commandResponse) {
            validatePoint(point);
            if (point.normalizedCommand <= previousCommand) {
                throw std::invalid_argument("SLM LUT commands must be strictly increasing");
            }
            previousCommand = point.normalizedCommand;
        }
        if (curve.commandResponse.front().normalizedCommand != 0.0
            || curve.commandResponse.back().normalizedCommand != 1.0) {
            throw std::invalid_argument("SLM LUT curves must include command endpoints 0 and 1");
        }
        previousWavelength = curve.vacuumWavelengthMetres;
    }
}

EvaluatedSlmResponse CalibratedSlmResponse::evaluate(
    double vacuumWavelengthMetres,
    double normalizedCommand) const {
    requireFinite(vacuumWavelengthMetres, "SLM evaluation wavelength must be finite");
    requireFinite(normalizedCommand, "SLM evaluation command must be finite");
    if (normalizedCommand < 0.0 || normalizedCommand > 1.0) {
        throw std::invalid_argument("SLM evaluation command must be in [0, 1]");
    }
    if (vacuumWavelengthMetres < wavelengths_.front().vacuumWavelengthMetres
        || vacuumWavelengthMetres > wavelengths_.back().vacuumWavelengthMetres) {
        throw std::out_of_range("SLM wavelength is outside the calibration domain");
    }
    const auto upper = std::lower_bound(
        wavelengths_.begin(),
        wavelengths_.end(),
        vacuumWavelengthMetres,
        [](const SlmWavelengthResponse& curve, double requested) {
            return curve.vacuumWavelengthMetres < requested;
        });
    if (upper == wavelengths_.begin()
        || upper->vacuumWavelengthMetres == vacuumWavelengthMetres) {
        return evaluateCurve(*upper, normalizedCommand);
    }
    const auto& lower = *(upper - 1);
    const auto lowerResponse = evaluateCurve(lower, normalizedCommand);
    const auto upperResponse = evaluateCurve(*upper, normalizedCommand);
    const double fraction = (vacuumWavelengthMetres - lower.vacuumWavelengthMetres)
        / (upper->vacuumWavelengthMetres - lower.vacuumWavelengthMetres);
    return {
        .amplitudeTransmission = std::lerp(
            lowerResponse.amplitudeTransmission,
            upperResponse.amplitudeTransmission,
            fraction),
        .unwrappedPhaseDelayRadians = std::lerp(
            lowerResponse.unwrappedPhaseDelayRadians,
            upperResponse.unwrappedPhaseDelayRadians,
            fraction),
    };
}

LcdColorChannel lcdColorChannelAt(
    std::size_t row,
    std::size_t column,
    LcdColorFilterPattern pattern) {
    switch (pattern) {
    case LcdColorFilterPattern::Monochrome:
        return LcdColorChannel::Green;
    case LcdColorFilterPattern::VerticalRgbStripes:
        return static_cast<LcdColorChannel>(column % 3U);
    case LcdColorFilterPattern::HorizontalRgbStripes:
        return static_cast<LcdColorChannel>(row % 3U);
    case LcdColorFilterPattern::BayerRggb:
        if ((row % 2U) == 0U) {
            return (column % 2U) == 0U ? LcdColorChannel::Red : LcdColorChannel::Green;
        }
        return (column % 2U) == 0U ? LcdColorChannel::Green : LcdColorChannel::Blue;
    }
    throw std::invalid_argument("unsupported LCD color-filter pattern");
}

std::complex<double> evaluateLcdTeachingTransfer(
    const LcdTeachingParameters& parameters,
    LcdColorChannel channel,
    double vacuumWavelengthMetres,
    double normalizedCommand) {
    validateLcdParameters(parameters);
    requireFinite(vacuumWavelengthMetres, "LCD wavelength must be finite");
    requireFinite(normalizedCommand, "LCD command must be finite");
    if (vacuumWavelengthMetres <= 0.0) {
        throw std::invalid_argument("LCD wavelength must be positive");
    }
    if (normalizedCommand < 0.0 || normalizedCommand > 1.0) {
        throw std::invalid_argument("LCD command must be in [0, 1]");
    }
    const double retardance = std::lerp(
        parameters.zeroCommandRetardanceRadians,
        parameters.fullCommandRetardanceRadians,
        normalizedCommand);
    const double inputRelative = parameters.inputPolarizerAngleRadians
        - parameters.liquidCrystalFastAxisAngleRadians;
    const double analyzerRelative = parameters.analyzerAngleRadians
        - parameters.liquidCrystalFastAxisAngleRadians;
    const double fastProjection = std::cos(analyzerRelative) * std::cos(inputRelative);
    const double slowProjection = std::sin(analyzerRelative) * std::sin(inputRelative);
    const auto retarderProjection = std::complex<double>(fastProjection, 0.0)
        + slowProjection * std::polar(
            1.0, std::remainder(retardance, 2.0 * std::numbers::pi));
    const double filterAmplitude = interpolateFilterAmplitude(
        parameters, channel, vacuumWavelengthMetres);
    const auto transfer = filterAmplitude * retarderProjection;
    if (!std::isfinite(transfer.real()) || !std::isfinite(transfer.imag())
        || std::abs(transfer) > 1.0 + 16.0 * std::numeric_limits<double>::epsilon()) {
        throw std::overflow_error("LCD teaching transfer is outside the passive finite domain");
    }
    return transfer;
}

} // namespace holobench::optics::slm
