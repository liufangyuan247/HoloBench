#include "optics/slm/SpatialLightModulator.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <utility>
#include <vector>

#include "core/field/ComplexField2D.hpp"
#include "optics/slm/SlmResponse.hpp"

namespace holobench::optics::slm {
namespace {

constexpr unsigned int maximumUsefulBitDepth = 52;

void requireFinite(double value, const char* message) {
    if (!std::isfinite(value)) {
        throw std::invalid_argument(message);
    }
}

void requireFiniteField(const field::ComplexField2D& value) {
    for (const auto sample : value.samples()) {
        if (!std::isfinite(sample.real()) || !std::isfinite(sample.imag())) {
            throw std::invalid_argument("SLM input field samples must be finite");
        }
    }
}

void requireCommands(
    std::span<const double> commands,
    std::size_t expectedCount,
    bool normalized) {
    if (commands.size() != expectedCount) {
        throw std::invalid_argument("SLM command count does not match its sampling grid");
    }
    for (const double command : commands) {
        if (!std::isfinite(command)) {
            throw std::invalid_argument("SLM commands must be finite");
        }
        if (normalized && (command < 0.0 || command > 1.0)) {
            throw std::invalid_argument("normalized SLM commands must be in [0, 1]");
        }
    }
}

[[nodiscard]] std::complex<double> finitePhasor(double phaseRadians) {
    const double reduced = std::remainder(phaseRadians, 2.0 * std::numbers::pi);
    if (!std::isfinite(reduced)) {
        throw std::overflow_error("SLM phase is not representable");
    }
    const auto result = std::polar(1.0, reduced);
    if (!std::isfinite(result.real()) || !std::isfinite(result.imag())) {
        throw std::overflow_error("SLM phase produced a non-finite phasor");
    }
    return result;
}

void multiplyFinite(
    field::ComplexField2D::Sample& destination,
    std::complex<double> transfer) {
    const auto result = destination * transfer;
    if (!std::isfinite(result.real()) || !std::isfinite(result.imag())) {
        throw std::overflow_error("SLM modulation produced a non-finite field sample");
    }
    destination = result;
}

[[nodiscard]] std::size_t checkedPixelCount(const PixelatedSlmParameters& parameters) {
    if (parameters.pixelColumns == 0 || parameters.pixelRows == 0) {
        throw std::invalid_argument("SLM pixel dimensions must be nonzero");
    }
    if (parameters.pixelColumns > std::numeric_limits<std::size_t>::max()
            / parameters.pixelRows) {
        throw std::overflow_error("SLM pixel count overflows size_t");
    }
    return parameters.pixelColumns * parameters.pixelRows;
}

void validateParameters(const PixelatedSlmParameters& parameters) {
    requireFinite(parameters.pixelPitchXMetres, "SLM X pixel pitch must be finite");
    requireFinite(parameters.pixelPitchYMetres, "SLM Y pixel pitch must be finite");
    requireFinite(parameters.fillFactorX, "SLM X fill factor must be finite");
    requireFinite(parameters.fillFactorY, "SLM Y fill factor must be finite");
    requireFinite(parameters.centerXMetres, "SLM X center must be finite");
    requireFinite(parameters.centerYMetres, "SLM Y center must be finite");
    requireFinite(parameters.phaseOffsetRadians, "SLM phase offset must be finite");
    requireFinite(parameters.phaseRangeRadians, "SLM phase range must be finite");
    if (parameters.pixelPitchXMetres <= 0.0 || parameters.pixelPitchYMetres <= 0.0) {
        throw std::invalid_argument("SLM pixel pitches must be positive");
    }
    if (parameters.fillFactorX <= 0.0 || parameters.fillFactorX > 1.0
        || parameters.fillFactorY <= 0.0 || parameters.fillFactorY > 1.0) {
        throw std::invalid_argument("SLM fill factors must be in (0, 1]");
    }
    if (parameters.phaseRangeRadians < 0.0) {
        throw std::invalid_argument("SLM phase range must be non-negative");
    }
    if (parameters.bitDepth > maximumUsefulBitDepth) {
        throw std::invalid_argument("SLM bit depth exceeds double-precision command resolution");
    }

    const double width = static_cast<double>(parameters.pixelColumns)
        * parameters.pixelPitchXMetres;
    const double height = static_cast<double>(parameters.pixelRows)
        * parameters.pixelPitchYMetres;
    if (!std::isfinite(width) || !std::isfinite(height)
        || !std::isfinite(parameters.centerXMetres - 0.5 * width)
        || !std::isfinite(parameters.centerXMetres + 0.5 * width)
        || !std::isfinite(parameters.centerYMetres - 0.5 * height)
        || !std::isfinite(parameters.centerYMetres + 0.5 * height)) {
        throw std::overflow_error("SLM active-area extent is not representable");
    }
}

[[nodiscard]] double quantizeCommand(double command, unsigned int bitDepth) {
    if (bitDepth == 0) {
        return command;
    }
    const double maximumCode = std::ldexp(1.0, static_cast<int>(bitDepth)) - 1.0;
    return std::round(command * maximumCode) / maximumCode;
}

struct PixelLocation final {
    bool insideGrid = false;
    bool insideActivePixel = false;
    std::size_t column = 0;
    std::size_t row = 0;
};

[[nodiscard]] double snapNearBoundary(double value, double boundary, double scale) noexcept {
    const double tolerance = 32.0 * std::numeric_limits<double>::epsilon()
        * std::max({1.0, std::abs(value), std::abs(boundary), std::abs(scale)});
    return std::abs(value - boundary) <= tolerance ? boundary : value;
}

[[nodiscard]] PixelLocation locatePixel(
    double xMetres,
    double yMetres,
    const PixelatedSlmParameters& parameters) {
    if (!std::isfinite(xMetres) || !std::isfinite(yMetres)) {
        throw std::overflow_error("SLM field coordinate is not representable");
    }
    const double width = static_cast<double>(parameters.pixelColumns)
        * parameters.pixelPitchXMetres;
    const double height = static_cast<double>(parameters.pixelRows)
        * parameters.pixelPitchYMetres;
    const double left = parameters.centerXMetres - 0.5 * width;
    const double bottom = parameters.centerYMetres - 0.5 * height;
    double gridX = (xMetres - left) / parameters.pixelPitchXMetres;
    double gridY = (yMetres - bottom) / parameters.pixelPitchYMetres;
    if (!std::isfinite(gridX) || !std::isfinite(gridY)) {
        throw std::overflow_error("SLM normalized field coordinate is not representable");
    }
    gridX = snapNearBoundary(gridX, std::round(gridX), gridX);
    gridY = snapNearBoundary(gridY, std::round(gridY), gridY);
    if (gridX < 0.0 || gridY < 0.0
        || gridX >= static_cast<double>(parameters.pixelColumns)
        || gridY >= static_cast<double>(parameters.pixelRows)) {
        return {};
    }

    const auto column = static_cast<std::size_t>(std::floor(gridX));
    const auto row = static_cast<std::size_t>(std::floor(gridY));
    double localX = gridX - (static_cast<double>(column) + 0.5);
    double localY = gridY - (static_cast<double>(row) + 0.5);
    const double halfFillX = 0.5 * parameters.fillFactorX;
    const double halfFillY = 0.5 * parameters.fillFactorY;
    localX = snapNearBoundary(
        snapNearBoundary(localX, -halfFillX, gridX), halfFillX, gridX);
    localY = snapNearBoundary(
        snapNearBoundary(localY, -halfFillY, gridY), halfFillY, gridY);
    const bool active = localX >= -halfFillX
        && localX < halfFillX
        && localY >= -halfFillY
        && localY < halfFillY;
    return {
        .insideGrid = true,
        .insideActivePixel = active,
        .column = column,
        .row = row,
    };
}

[[nodiscard]] SlmApplicationDiagnostics applyPixelTransfers(
    field::ComplexField2D& field,
    const PixelatedSlmParameters& parameters,
    std::span<const std::complex<double>> transfers,
    std::span<const unsigned char> quantizedCommands) {
    const auto pixelCount = checkedPixelCount(parameters);
    validateParameters(parameters);
    if (transfers.size() != pixelCount) {
        throw std::invalid_argument("SLM transfer count does not match its pixel grid");
    }
    if (!quantizedCommands.empty() && quantizedCommands.size() != pixelCount) {
        throw std::invalid_argument("SLM quantization flags do not match its pixel grid");
    }
    for (const auto transfer : transfers) {
        if (!std::isfinite(transfer.real()) || !std::isfinite(transfer.imag())) {
            throw std::invalid_argument("SLM pixel transfers must be finite");
        }
    }
    requireFiniteField(field);

    auto modulated = field;
    SlmApplicationDiagnostics diagnostics;
    for (std::size_t y = 0; y < modulated.height(); ++y) {
        const double yMetres = modulated.yCoordinateMetres(y);
        for (std::size_t x = 0; x < modulated.width(); ++x) {
            auto& sample = modulated.at(x, y);
            const auto location = locatePixel(
                modulated.xCoordinateMetres(x), yMetres, parameters);
            if (!location.insideGrid) {
                sample = {0.0, 0.0};
                ++diagnostics.outsideActiveAreaSampleCount;
                continue;
            }
            if (!location.insideActivePixel) {
                sample = {0.0, 0.0};
                ++diagnostics.deadSpaceSampleCount;
                continue;
            }
            const std::size_t commandIndex = location.row * parameters.pixelColumns
                + location.column;
            multiplyFinite(sample, transfers[commandIndex]);
            if (!quantizedCommands.empty() && quantizedCommands[commandIndex] != 0U) {
                ++diagnostics.quantizedSampleCount;
            }
            ++diagnostics.modulatedSampleCount;
        }
    }
    field = std::move(modulated);
    return diagnostics;
}

} // namespace

SlmApplicationDiagnostics applyIdealAmplitudeSlm(
    field::ComplexField2D& field,
    std::span<const double> amplitudeCommands) {
    requireCommands(amplitudeCommands, field.sampleCount(), true);
    requireFiniteField(field);

    auto modulated = field;
    for (std::size_t index = 0; index < modulated.sampleCount(); ++index) {
        multiplyFinite(modulated.samples()[index], {amplitudeCommands[index], 0.0});
    }
    field = std::move(modulated);
    return {.modulatedSampleCount = field.sampleCount()};
}

SlmApplicationDiagnostics applyIdealPhaseSlm(
    field::ComplexField2D& field,
    std::span<const double> phaseCommandsRadians) {
    requireCommands(phaseCommandsRadians, field.sampleCount(), false);
    requireFiniteField(field);

    auto modulated = field;
    for (std::size_t index = 0; index < modulated.sampleCount(); ++index) {
        multiplyFinite(modulated.samples()[index], finitePhasor(phaseCommandsRadians[index]));
    }
    field = std::move(modulated);
    return {.modulatedSampleCount = field.sampleCount()};
}

SlmApplicationDiagnostics applyPixelatedSlm(
    field::ComplexField2D& field,
    const PixelatedSlmParameters& parameters,
    std::span<const double> normalizedPixelCommands) {
    const auto pixelCount = checkedPixelCount(parameters);
    validateParameters(parameters);
    requireCommands(normalizedPixelCommands, pixelCount, true);

    std::vector<std::complex<double>> transfers(pixelCount);
    std::vector<unsigned char> quantized(pixelCount, 0U);
    for (std::size_t index = 0; index < pixelCount; ++index) {
        const double command = normalizedPixelCommands[index];
        const double effectiveCommand = quantizeCommand(command, parameters.bitDepth);
        quantized[index] = effectiveCommand != command ? 1U : 0U;
        if (parameters.mode == ModulationMode::Amplitude) {
            transfers[index] = {effectiveCommand, 0.0};
        } else {
            const double phase = std::fma(
                effectiveCommand,
                parameters.phaseRangeRadians,
                parameters.phaseOffsetRadians);
            if (!std::isfinite(phase)) {
                throw std::overflow_error("SLM command phase is not representable");
            }
            transfers[index] = finitePhasor(phase);
        }
    }
    return applyPixelTransfers(field, parameters, transfers, quantized);
}

SlmApplicationDiagnostics applyCalibratedPixelatedSlm(
    field::ComplexField2D& field,
    const PixelatedSlmParameters& parameters,
    std::span<const double> normalizedPixelCommands,
    const CalibratedSlmResponse& response) {
    const auto pixelCount = checkedPixelCount(parameters);
    validateParameters(parameters);
    requireCommands(normalizedPixelCommands, pixelCount, true);
    std::vector<std::complex<double>> transfers(pixelCount);
    std::vector<unsigned char> quantized(pixelCount, 0U);
    for (std::size_t index = 0; index < pixelCount; ++index) {
        const double command = normalizedPixelCommands[index];
        const double effectiveCommand = quantizeCommand(command, parameters.bitDepth);
        quantized[index] = effectiveCommand != command ? 1U : 0U;
        transfers[index] = response.evaluate(
            field.vacuumWavelengthMetres(), effectiveCommand).complexTransfer();
    }
    return applyPixelTransfers(field, parameters, transfers, quantized);
}

SlmApplicationDiagnostics applyLcdTeachingSlm(
    field::ComplexField2D& field,
    const PixelatedSlmParameters& parameters,
    std::span<const double> normalizedPixelCommands,
    const LcdTeachingParameters& lcdParameters) {
    const auto pixelCount = checkedPixelCount(parameters);
    validateParameters(parameters);
    requireCommands(normalizedPixelCommands, pixelCount, true);
    std::vector<std::complex<double>> transfers(pixelCount);
    std::vector<unsigned char> quantized(pixelCount, 0U);
    for (std::size_t row = 0; row < parameters.pixelRows; ++row) {
        for (std::size_t column = 0; column < parameters.pixelColumns; ++column) {
            const std::size_t index = row * parameters.pixelColumns + column;
            const double command = normalizedPixelCommands[index];
            const double effectiveCommand = quantizeCommand(command, parameters.bitDepth);
            quantized[index] = effectiveCommand != command ? 1U : 0U;
            transfers[index] = evaluateLcdTeachingTransfer(
                lcdParameters,
                lcdColorChannelAt(row, column, lcdParameters.colorFilterPattern),
                field.vacuumWavelengthMetres(),
                effectiveCommand);
        }
    }
    return applyPixelTransfers(field, parameters, transfers, quantized);
}

} // namespace holobench::optics::slm
