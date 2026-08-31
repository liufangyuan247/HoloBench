#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <complex>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "compute/fft/CpuFftBackend.hpp"
#include "compute/fourier/FourierOptics.hpp"
#include "compute/propagation/AngularSpectrumPropagator.hpp"
#include "compute/propagation/FraunhoferPropagator.hpp"
#include "compute/propagation/FresnelPropagator.hpp"
#include "core/field/ComplexField2D.hpp"
#include "optics/slm/SpatialLightModulator.hpp"

#ifndef HOLOBENCH_WAVEPROP_GOLDEN_DIR
#error "HOLOBENCH_WAVEPROP_GOLDEN_DIR must identify the configured golden-data directory"
#endif

namespace {

struct GoldenCase final {
    std::string name;
    std::string generator;
    std::size_t width = 0;
    std::size_t height = 0;
    double inputPitchXMetres = 0.0;
    double inputPitchYMetres = 0.0;
    double outputPitchXMetres = 0.0;
    double outputPitchYMetres = 0.0;
    double wavelengthMetres = 0.0;
    double refractiveIndex = 0.0;
    double distanceMetres = 0.0;
    std::vector<std::complex<double>> input;
    std::vector<std::complex<double>> expectedOutput;
    std::vector<double> inputX;
    std::vector<double> inputY;
    std::vector<double> outputX;
    std::vector<double> outputY;
};

struct ErrorMetrics final {
    double normalizedL2 = 0.0;
    double maximumErrorRelativeToPeak = 0.0;
    double normalizedIntensityL2 = 0.0;
    double maximumIntensityErrorRelativeToPeak = 0.0;
    double referencePeak = 0.0;
};

[[nodiscard]] std::filesystem::path goldenDirectory() {
    return std::filesystem::path(HOLOBENCH_WAVEPROP_GOLDEN_DIR);
}

[[nodiscard]] double parseDouble(std::string_view text) {
    double value = 0.0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()
        || !std::isfinite(value)) {
        throw std::runtime_error("invalid finite floating-point value in waveprop CSV");
    }
    return value;
}

[[nodiscard]] std::array<double, 8> parseCsvRow(const std::string& line) {
    std::array<double, 8> values{};
    std::size_t begin = 0;
    for (std::size_t column = 0; column < values.size(); ++column) {
        const std::size_t end = line.find(',', begin);
        if (column + 1U < values.size() && end == std::string::npos) {
            throw std::runtime_error("waveprop CSV row has too few columns");
        }
        if (column + 1U == values.size() && end != std::string::npos) {
            throw std::runtime_error("waveprop CSV row has too many columns");
        }
        const std::size_t tokenEnd = end == std::string::npos ? line.size() : end;
        values[column] = parseDouble(
            std::string_view(line).substr(begin, tokenEnd - begin));
        begin = tokenEnd + 1U;
    }
    return values;
}

[[nodiscard]] GoldenCase loadGoldenCase(const std::string& stem) {
    const auto directory = goldenDirectory();
    std::ifstream metadataStream(directory / (stem + ".json"));
    if (!metadataStream) {
        throw std::runtime_error("cannot open waveprop metadata for " + stem);
    }
    nlohmann::json metadata;
    metadataStream >> metadata;

    GoldenCase result;
    result.name = metadata.at("name").get<std::string>();
    result.generator = metadata.at("generator").get<std::string>();
    result.width = metadata.at("width").get<std::size_t>();
    result.height = metadata.at("height").get<std::size_t>();
    result.inputPitchXMetres = metadata.at("input_pitch_x_metres").get<double>();
    result.inputPitchYMetres = metadata.at("input_pitch_y_metres").get<double>();
    result.outputPitchXMetres = metadata.at("output_pitch_x_metres").get<double>();
    result.outputPitchYMetres = metadata.at("output_pitch_y_metres").get<double>();
    result.wavelengthMetres = metadata.at("vacuum_wavelength_metres").get<double>();
    result.refractiveIndex = metadata.at("refractive_index").get<double>();
    result.distanceMetres = metadata.at("propagation_distance_metres").get<double>();
    if (result.width == 0U || result.height == 0U
        || result.width > std::numeric_limits<std::size_t>::max() / result.height) {
        throw std::runtime_error("invalid waveprop golden dimensions");
    }
    const std::size_t sampleCount = result.width * result.height;
    result.input.reserve(sampleCount);
    result.expectedOutput.reserve(sampleCount);
    result.inputX.reserve(sampleCount);
    result.inputY.reserve(sampleCount);
    result.outputX.reserve(sampleCount);
    result.outputY.reserve(sampleCount);

    std::ifstream dataStream(directory / (stem + ".csv"));
    if (!dataStream) {
        throw std::runtime_error("cannot open waveprop samples for " + stem);
    }
    std::string line;
    if (!std::getline(dataStream, line)) {
        throw std::runtime_error("waveprop CSV is empty");
    }
    while (std::getline(dataStream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            throw std::runtime_error("waveprop CSV contains an empty data row");
        }
        const auto values = parseCsvRow(line);
        result.inputX.push_back(values[0]);
        result.inputY.push_back(values[1]);
        result.input.emplace_back(values[2], values[3]);
        result.outputX.push_back(values[4]);
        result.outputY.push_back(values[5]);
        result.expectedOutput.emplace_back(values[6], values[7]);
    }
    if (result.input.size() != sampleCount) {
        throw std::runtime_error("waveprop CSV sample count does not match metadata");
    }
    return result;
}

[[nodiscard]] holobench::field::ComplexField2D makeInputField(const GoldenCase& golden) {
    holobench::field::ComplexField2D field(
        golden.width,
        golden.height,
        golden.inputPitchXMetres,
        golden.inputPitchYMetres,
        golden.wavelengthMetres,
        golden.refractiveIndex);
    for (std::size_t index = 0; index < golden.input.size(); ++index) {
        field.samples()[index] = golden.input[index];
    }
    return field;
}

[[nodiscard]] ErrorMetrics calculateErrorMetrics(
    std::span<const std::complex<double>> actual,
    std::span<const std::complex<double>> expected) {
    if (actual.size() != expected.size() || actual.empty()) {
        throw std::invalid_argument("metric inputs must have equal non-zero sizes");
    }
    long double squaredError = 0.0L;
    long double squaredReference = 0.0L;
    long double squaredIntensityError = 0.0L;
    long double squaredReferenceIntensity = 0.0L;
    double maximumError = 0.0;
    double maximumIntensityError = 0.0;
    double referencePeak = 0.0;
    double referenceIntensityPeak = 0.0;
    for (std::size_t index = 0; index < actual.size(); ++index) {
        const double error = std::abs(actual[index] - expected[index]);
        const double reference = std::abs(expected[index]);
        squaredError += static_cast<long double>(error) * static_cast<long double>(error);
        squaredReference += static_cast<long double>(reference) * static_cast<long double>(reference);
        maximumError = std::max(maximumError, error);
        referencePeak = std::max(referencePeak, reference);
        const double actualIntensity = std::norm(actual[index]);
        const double referenceIntensity = std::norm(expected[index]);
        const double intensityError = std::abs(actualIntensity - referenceIntensity);
        squaredIntensityError += static_cast<long double>(intensityError)
            * static_cast<long double>(intensityError);
        squaredReferenceIntensity += static_cast<long double>(referenceIntensity)
            * static_cast<long double>(referenceIntensity);
        maximumIntensityError = std::max(maximumIntensityError, intensityError);
        referenceIntensityPeak = std::max(referenceIntensityPeak, referenceIntensity);
    }
    if (!(squaredReference > 0.0L) || !(referencePeak > 0.0)) {
        throw std::runtime_error("waveprop reference field has zero energy");
    }
    return ErrorMetrics{
        std::sqrt(static_cast<double>(squaredError / squaredReference)),
        maximumError / referencePeak,
        std::sqrt(static_cast<double>(squaredIntensityError / squaredReferenceIntensity)),
        maximumIntensityError / referenceIntensityPeak,
        referencePeak};
}

void checkCoordinates(
    const GoldenCase& golden,
    const holobench::field::ComplexField2D& input,
    const holobench::field::ComplexField2D& output) {
    double maximumInputError = 0.0;
    double maximumOutputError = 0.0;
    for (std::size_t y = 0; y < golden.height; ++y) {
        for (std::size_t x = 0; x < golden.width; ++x) {
            const std::size_t index = y * golden.width + x;
            maximumInputError = std::max(
                maximumInputError,
                std::abs(input.xCoordinateMetres(x) - golden.inputX[index]));
            maximumInputError = std::max(
                maximumInputError,
                std::abs(input.yCoordinateMetres(y) - golden.inputY[index]));
            maximumOutputError = std::max(
                maximumOutputError,
                std::abs(output.xCoordinateMetres(x) - golden.outputX[index]));
            maximumOutputError = std::max(
                maximumOutputError,
                std::abs(output.yCoordinateMetres(y) - golden.outputY[index]));
        }
    }
    INFO("maximum input-coordinate error metres: " << maximumInputError);
    INFO("maximum output-coordinate error metres: " << maximumOutputError);
    CHECK(maximumInputError < 1e-18);
    CHECK(maximumOutputError < 1e-15);
}

void checkMetrics(const ErrorMetrics& metrics, double l2Tolerance, double peakTolerance) {
    INFO("normalized complex L2 error: " << metrics.normalizedL2);
    INFO("maximum complex error / reference peak: " << metrics.maximumErrorRelativeToPeak);
    INFO("normalized intensity L2 error: " << metrics.normalizedIntensityL2);
    INFO("maximum intensity error / reference peak: "
        << metrics.maximumIntensityErrorRelativeToPeak);
    INFO("reference peak amplitude: " << metrics.referencePeak);
    CHECK(metrics.normalizedL2 < l2Tolerance);
    CHECK(metrics.maximumErrorRelativeToPeak < peakTolerance);
    CHECK(metrics.normalizedIntensityL2 < 1e-10);
    CHECK(metrics.maximumIntensityErrorRelativeToPeak < 1e-10);
}

} // namespace

TEST_SUITE("waveprop external cross-validation") {

TEST_CASE("ASM rectangular tilted Gaussian agrees with waveprop 0.0.12") {
    const auto golden = loadGoldenCase("asm_rectangular_tilted_gaussian");
    CHECK(golden.generator == "waveprop.rs.angular_spectrum");
    auto field = makeInputField(golden);
    const auto input = field;
    holobench::compute::fft::CpuFftBackend fft;
    holobench::compute::propagation::AngularSpectrumPropagator propagator(fft);
    static_cast<void>(propagator.propagateInPlace(field, golden.distanceMetres));

    checkCoordinates(golden, input, field);
    checkMetrics(calculateErrorMetrics(field.samples(), golden.expectedOutput), 5e-11, 5e-11);
}

TEST_CASE("Fresnel transfer-function Gaussian agrees with waveprop 0.0.12") {
    const auto golden = loadGoldenCase("fresnel_tf_square_gaussian");
    CHECK(golden.generator == "waveprop.fresnel.fresnel_conv");
    auto field = makeInputField(golden);
    const auto input = field;
    holobench::compute::fft::CpuFftBackend fft;
    holobench::compute::propagation::FresnelTransferFunctionPropagator propagator(fft);
    static_cast<void>(propagator.propagateInPlace(field, golden.distanceMetres));

    checkCoordinates(golden, input, field);
    checkMetrics(calculateErrorMetrics(field.samples(), golden.expectedOutput), 5e-11, 5e-11);
}

TEST_CASE("Fraunhofer rectangular double slit agrees with waveprop 0.0.12") {
    const auto golden = loadGoldenCase("fraunhofer_rectangular_double_slit");
    CHECK(golden.generator == "waveprop.fraunhofer.fraunhofer");
    const auto input = makeInputField(golden);
    holobench::compute::fft::CpuFftBackend fft;
    holobench::compute::propagation::FraunhoferPropagator propagator(fft);
    const auto result = propagator.propagate(input, golden.distanceMetres);

    checkCoordinates(golden, input, result.field);
    CHECK(result.diagnostics.outputPitchXMetres == doctest::Approx(golden.outputPitchXMetres));
    CHECK(result.diagnostics.outputPitchYMetres == doctest::Approx(golden.outputPitchYMetres));
    checkMetrics(
        calculateErrorMetrics(result.field.samples(), golden.expectedOutput),
        // Both solvers agree in amplitude much more tightly; this complex-field
        // tolerance covers their different double-precision range reduction of
        // the 1 m longitudinal carrier phase (k*z is approximately 9.9e6 rad).
        5e-9,
        5e-9);
}

TEST_CASE("selected pixel SLM mask and angular intensity agree with waveprop 0.0.12") {
    const auto golden = loadGoldenCase("slm_selected_pixel_fraunhofer");
    CHECK(golden.generator
        == "waveprop.slm.get_slm_mask + waveprop.fraunhofer.fraunhofer");
    holobench::field::ComplexField2D input(
        golden.width,
        golden.height,
        golden.inputPitchXMetres,
        golden.inputPitchYMetres,
        golden.wavelengthMetres,
        golden.refractiveIndex);
    input.fill({1.0, 0.0});
    holobench::optics::slm::PixelatedSlmParameters parameters;
    parameters.pixelColumns = 16;
    parameters.pixelRows = 8;
    parameters.pixelPitchXMetres = 8e-6;
    parameters.pixelPitchYMetres = 8e-6;
    parameters.fillFactorX = 0.75;
    parameters.fillFactorY = 0.75;
    // waveprop 0.0.12 rasterizes X cells one sample to the right. The golden
    // metadata records this explicit coordinate conversion rather than
    // shifting either result after generation.
    parameters.centerXMetres = golden.inputPitchXMetres;
    parameters.mode = holobench::optics::slm::ModulationMode::Amplitude;
    std::vector<double> commands(parameters.pixelColumns * parameters.pixelRows, 0.0);
    commands[5U * parameters.pixelColumns + 12U] = 1.0;
    const auto slmDiagnostics = holobench::optics::slm::applyPixelatedSlm(
        input, parameters, commands);

    double maximumMaskError = 0.0;
    for (std::size_t index = 0; index < input.sampleCount(); ++index) {
        maximumMaskError = std::max(
            maximumMaskError, std::abs(input.samples()[index] - golden.input[index]));
    }
    INFO("maximum waveprop SLM mask error: " << maximumMaskError);
    CHECK(maximumMaskError < 1e-15);
    CHECK(slmDiagnostics.modulatedSampleCount == 48U * 96U);
    CHECK(slmDiagnostics.deadSpaceSampleCount == 64U * 127U - 48U * 96U);
    CHECK(slmDiagnostics.outsideActiveAreaSampleCount == 64U);

    holobench::compute::fft::CpuFftBackend fft;
    holobench::compute::fourier::FourierLensTransform lens(fft);
    const auto result = lens.transformFrontToBackFocalPlane(input, golden.distanceMetres);
    checkCoordinates(golden, input, result.field);
    CHECK(result.diagnostics.outputPitchXMetres
        == doctest::Approx(golden.outputPitchXMetres));
    CHECK(result.diagnostics.outputPitchYMetres
        == doctest::Approx(golden.outputPitchYMetres));
    const auto metrics = calculateErrorMetrics(
        result.field.samples(), golden.expectedOutput);
    INFO("SLM angular normalized intensity L2 error: "
        << metrics.normalizedIntensityL2);
    INFO("SLM angular maximum intensity error / reference peak: "
        << metrics.maximumIntensityErrorRelativeToPeak);
    // waveprop's Fraunhofer field includes an output quadratic phase that is
    // absent from the ideal front/back focal-plane ABCD transform. Intensity,
    // physical coordinates, and normalization are the independent common
    // observables; complex phase is intentionally not compared here.
    CHECK(metrics.normalizedIntensityL2 < 1e-12);
    CHECK(metrics.maximumIntensityErrorRelativeToPeak < 1e-12);
}

} // TEST_SUITE("waveprop external cross-validation")
