#include "app/WaveWorkbenchProject.hpp"

#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

namespace holobench::app::waveproject {
namespace {

using Json = nlohmann::json;

void requireKeys(
    const Json& object,
    const std::set<std::string>& expected,
    const char* context) {
    if (!object.is_object()) {
        throw std::invalid_argument(std::string(context) + " must be an object");
    }
    std::set<std::string> actual;
    for (auto iterator = object.begin(); iterator != object.end(); ++iterator) {
        actual.insert(iterator.key());
    }
    if (actual != expected) {
        throw std::invalid_argument(
            std::string(context) + " has missing or unknown keys");
    }
}

[[nodiscard]] double finiteNumber(const Json& value, const char* context) {
    if (!value.is_number()) {
        throw std::invalid_argument(std::string(context) + " must be numeric");
    }
    const double result = value.get<double>();
    if (!std::isfinite(result)) {
        throw std::invalid_argument(std::string(context) + " must be finite");
    }
    return result;
}

[[nodiscard]] std::size_t sizeValue(const Json& value, const char* context) {
    if (!value.is_number_integer()) {
        throw std::invalid_argument(std::string(context) + " must be an integer");
    }
    const auto signedValue = value.get<std::int64_t>();
    if (signedValue < 0
        || static_cast<std::uint64_t>(signedValue)
            > static_cast<std::uint64_t>(
                std::numeric_limits<std::size_t>::max())) {
        throw std::invalid_argument(std::string(context) + " is outside size_t range");
    }
    return static_cast<std::size_t>(signedValue);
}

void requireFinite(double value, const char* context) {
    if (!std::isfinite(value)) {
        throw std::invalid_argument(std::string(context) + " must be finite");
    }
}

void requirePositive(double value, const char* context) {
    if (!std::isfinite(value) || value <= 0.0) {
        throw std::invalid_argument(
            std::string(context) + " must be positive and finite");
    }
}

[[nodiscard]] Json provenanceJson(const project::ProjectProvenance& provenance) {
    project::validateProjectProvenance(provenance);
    return {
        {"origin", project::projectOriginKindName(provenance.originKind)},
        {"source_id", provenance.sourceId},
        {"source_version", provenance.sourceVersion},
    };
}

[[nodiscard]] project::ProjectProvenance parseProvenance(const Json& json) {
    requireKeys(
        json, {"origin", "source_id", "source_version"}, "project provenance");
    if (!json.at("origin").is_string()
        || !json.at("source_id").is_string()
        || !json.at("source_version").is_number_integer()) {
        throw std::invalid_argument("project provenance fields have invalid types");
    }
    const auto version = json.at("source_version").get<std::int64_t>();
    if (version < std::numeric_limits<int>::min()
        || version > std::numeric_limits<int>::max()) {
        throw std::invalid_argument("project provenance version is outside int range");
    }
    project::ProjectProvenance result {
        .originKind = project::projectOriginKindFromName(
            json.at("origin").get<std::string>()),
        .sourceId = json.at("source_id").get<std::string>(),
        .sourceVersion = static_cast<int>(version),
    };
    project::validateProjectProvenance(result);
    return result;
}

[[nodiscard]] const char* sourceKindName(wave::WaveSourceKind kind) {
    switch (kind) {
    case wave::WaveSourceKind::PlaneWave:
        return "plane_wave";
    case wave::WaveSourceKind::GaussianBeam:
        return "gaussian_beam";
    }
    throw std::invalid_argument("unsupported wave source kind");
}

[[nodiscard]] wave::WaveSourceKind parseSourceKind(const Json& value) {
    if (!value.is_string()) {
        throw std::invalid_argument("wave source kind must be a string");
    }
    const auto name = value.get<std::string>();
    if (name == "plane_wave") {
        return wave::WaveSourceKind::PlaneWave;
    }
    if (name == "gaussian_beam") {
        return wave::WaveSourceKind::GaussianBeam;
    }
    throw std::invalid_argument("unsupported wave source kind");
}

[[nodiscard]] const char* apertureKindName(wave::WaveApertureKind kind) {
    switch (kind) {
    case wave::WaveApertureKind::None:
        return "none";
    case wave::WaveApertureKind::Circular:
        return "circular";
    case wave::WaveApertureKind::Rectangular:
        return "rectangular";
    case wave::WaveApertureKind::DoubleSlit:
        return "double_slit";
    }
    throw std::invalid_argument("unsupported wave aperture kind");
}

[[nodiscard]] wave::WaveApertureKind parseApertureKind(const Json& value) {
    if (!value.is_string()) {
        throw std::invalid_argument("wave aperture kind must be a string");
    }
    const auto name = value.get<std::string>();
    if (name == "none") {
        return wave::WaveApertureKind::None;
    }
    if (name == "circular") {
        return wave::WaveApertureKind::Circular;
    }
    if (name == "rectangular") {
        return wave::WaveApertureKind::Rectangular;
    }
    if (name == "double_slit") {
        return wave::WaveApertureKind::DoubleSlit;
    }
    throw std::invalid_argument("unsupported wave aperture kind");
}

[[nodiscard]] const char* propagatorName(wave::WavePropagatorKind kind) {
    switch (kind) {
    case wave::WavePropagatorKind::AngularSpectrum:
        return "angular_spectrum";
    case wave::WavePropagatorKind::FresnelTransferFunction:
        return "fresnel_transfer_function";
    }
    throw std::invalid_argument("unsupported wave propagator kind");
}

[[nodiscard]] wave::WavePropagatorKind parsePropagator(const Json& value) {
    if (!value.is_string()) {
        throw std::invalid_argument("wave propagator must be a string");
    }
    const auto name = value.get<std::string>();
    if (name == "angular_spectrum") {
        return wave::WavePropagatorKind::AngularSpectrum;
    }
    if (name == "fresnel_transfer_function") {
        return wave::WavePropagatorKind::FresnelTransferFunction;
    }
    throw std::invalid_argument("unsupported wave propagator kind");
}

[[nodiscard]] const char* filterKindName(
    compute::fourier::CircularFilterKind kind) {
    switch (kind) {
    case compute::fourier::CircularFilterKind::PassAll:
        return "pass_all";
    case compute::fourier::CircularFilterKind::LowPass:
        return "low_pass";
    case compute::fourier::CircularFilterKind::HighPass:
        return "high_pass";
    case compute::fourier::CircularFilterKind::BandPass:
        return "band_pass";
    }
    throw std::invalid_argument("unsupported 4-f filter kind");
}

[[nodiscard]] compute::fourier::CircularFilterKind parseFilterKind(
    const Json& value) {
    if (!value.is_string()) {
        throw std::invalid_argument("4-f filter kind must be a string");
    }
    const auto name = value.get<std::string>();
    if (name == "pass_all") {
        return compute::fourier::CircularFilterKind::PassAll;
    }
    if (name == "low_pass") {
        return compute::fourier::CircularFilterKind::LowPass;
    }
    if (name == "high_pass") {
        return compute::fourier::CircularFilterKind::HighPass;
    }
    if (name == "band_pass") {
        return compute::fourier::CircularFilterKind::BandPass;
    }
    throw std::invalid_argument("unsupported 4-f filter kind");
}

[[nodiscard]] Json waveDetectorJson(const wave::WaveDetectorConfig& config) {
    return {
        {"aperture", {
            {"center_m", {config.apertureCenterXMetres, config.apertureCenterYMetres}},
            {"circular_radius_m", config.circularApertureRadiusMetres},
            {"double_slit_height_m", config.doubleSlitHeightMetres},
            {"double_slit_separation_m", config.doubleSlitSeparationMetres},
            {"double_slit_width_m", config.doubleSlitWidthMetres},
            {"kind", apertureKindName(config.apertureKind)},
            {"rectangular_half_height_m", config.rectangularHalfHeightMetres},
            {"rectangular_half_width_m", config.rectangularHalfWidthMetres},
        }},
        {"propagation", {
            {"distance_m", config.propagationDistanceMetres},
            {"grid_resolution", config.gridResolution},
            {"grid_span_m", config.gridPhysicalSpanMetres},
            {"propagator", propagatorName(config.propagator)},
            {"refractive_index", config.refractiveIndex},
        }},
        {"source", {
            {"amplitude", {config.sourceAmplitude.real(), config.sourceAmplitude.imag()}},
            {"gaussian_waist_radius_m", config.gaussianWaistRadiusMetres},
            {"kind", sourceKindName(config.sourceKind)},
            {"phase_at_origin_rad", config.sourcePhaseAtOriginRadians},
            {"plane_wave_direction_cosine", {
                config.planeWaveDirectionCosineX,
                config.planeWaveDirectionCosineY,
            }},
            {"vacuum_wavelength_m", config.wavelengthMetres},
        }},
        {"thin_lens", {
            {"center_m", {config.thinLensCenterXMetres, config.thinLensCenterYMetres}},
            {"enabled", config.enableThinLens},
            {"focal_length_m", config.thinLensFocalLengthMetres},
        }},
    };
}

[[nodiscard]] std::pair<double, double> numberPair(
    const Json& value,
    const char* context) {
    if (!value.is_array() || value.size() != 2U) {
        throw std::invalid_argument(
            std::string(context) + " must contain two numbers");
    }
    return {
        finiteNumber(value.at(0), context),
        finiteNumber(value.at(1), context),
    };
}

[[nodiscard]] wave::WaveDetectorConfig parseWaveDetector(const Json& json) {
    requireKeys(json, {"aperture", "propagation", "source", "thin_lens"},
        "wave detector config");
    wave::WaveDetectorConfig config;

    const auto& source = json.at("source");
    requireKeys(source, {
        "amplitude", "gaussian_waist_radius_m", "kind", "phase_at_origin_rad",
        "plane_wave_direction_cosine", "vacuum_wavelength_m",
    }, "wave source");
    config.sourceKind = parseSourceKind(source.at("kind"));
    config.wavelengthMetres = finiteNumber(
        source.at("vacuum_wavelength_m"), "wave wavelength");
    const auto amplitude = numberPair(source.at("amplitude"), "wave amplitude");
    config.sourceAmplitude = {amplitude.first, amplitude.second};
    config.gaussianWaistRadiusMetres = finiteNumber(
        source.at("gaussian_waist_radius_m"), "Gaussian waist radius");
    const auto direction = numberPair(
        source.at("plane_wave_direction_cosine"), "plane-wave direction cosine");
    config.planeWaveDirectionCosineX = direction.first;
    config.planeWaveDirectionCosineY = direction.second;
    config.sourcePhaseAtOriginRadians = finiteNumber(
        source.at("phase_at_origin_rad"), "source phase");

    const auto& aperture = json.at("aperture");
    requireKeys(aperture, {
        "center_m", "circular_radius_m", "double_slit_height_m",
        "double_slit_separation_m", "double_slit_width_m", "kind",
        "rectangular_half_height_m", "rectangular_half_width_m",
    }, "wave aperture");
    config.apertureKind = parseApertureKind(aperture.at("kind"));
    config.circularApertureRadiusMetres = finiteNumber(
        aperture.at("circular_radius_m"), "circular aperture radius");
    config.rectangularHalfWidthMetres = finiteNumber(
        aperture.at("rectangular_half_width_m"), "rectangular half width");
    config.rectangularHalfHeightMetres = finiteNumber(
        aperture.at("rectangular_half_height_m"), "rectangular half height");
    config.doubleSlitWidthMetres = finiteNumber(
        aperture.at("double_slit_width_m"), "double-slit width");
    config.doubleSlitHeightMetres = finiteNumber(
        aperture.at("double_slit_height_m"), "double-slit height");
    config.doubleSlitSeparationMetres = finiteNumber(
        aperture.at("double_slit_separation_m"), "double-slit separation");
    const auto apertureCenter = numberPair(
        aperture.at("center_m"), "aperture center");
    config.apertureCenterXMetres = apertureCenter.first;
    config.apertureCenterYMetres = apertureCenter.second;

    const auto& thinLens = json.at("thin_lens");
    requireKeys(thinLens, {"center_m", "enabled", "focal_length_m"},
        "wave thin lens");
    if (!thinLens.at("enabled").is_boolean()) {
        throw std::invalid_argument("thin-lens enabled must be boolean");
    }
    config.enableThinLens = thinLens.at("enabled").get<bool>();
    config.thinLensFocalLengthMetres = finiteNumber(
        thinLens.at("focal_length_m"), "thin-lens focal length");
    const auto lensCenter = numberPair(thinLens.at("center_m"), "thin-lens center");
    config.thinLensCenterXMetres = lensCenter.first;
    config.thinLensCenterYMetres = lensCenter.second;

    const auto& propagation = json.at("propagation");
    requireKeys(propagation, {
        "distance_m", "grid_resolution", "grid_span_m", "propagator",
        "refractive_index",
    }, "wave propagation");
    config.propagator = parsePropagator(propagation.at("propagator"));
    config.propagationDistanceMetres = finiteNumber(
        propagation.at("distance_m"), "wave propagation distance");
    config.gridResolution = sizeValue(
        propagation.at("grid_resolution"), "wave grid resolution");
    config.gridPhysicalSpanMetres = finiteNumber(
        propagation.at("grid_span_m"), "wave grid span");
    config.refractiveIndex = finiteNumber(
        propagation.at("refractive_index"), "wave refractive index");
    return config;
}

[[nodiscard]] Json optionalNumber(std::optional<double> value) {
    return value.has_value() ? Json(value.value()) : Json(nullptr);
}

[[nodiscard]] Json samplingDebuggerJson(
    const samplingdebug::SamplingDebuggerConfig& config) {
    return {
        {"four_f", {
            {"display_floor_db", config.fourFDisplayFloorDecibels},
            {"filter_inner_radius_m", config.fourFFilterInnerRadiusMetres},
            {"filter_kind", filterKindName(config.fourFFilterKind)},
            {"filter_outer_radius_m", config.fourFFilterOuterRadiusMetres},
            {"first_focal_length_m", config.fourFFirstFocalLengthMetres},
            {"second_focal_length_m", config.fourFSecondFocalLengthMetres},
        }},
        {"illuminated_extent_m", {
            optionalNumber(config.illuminatedExtentXMetres),
            optionalNumber(config.illuminatedExtentYMetres),
        }},
        {"minimum_boundary_guard_samples", config.minimumBoundaryGuardSamples},
        {"probe", {
            {"distances_m", config.probeDistancesMetres},
            {"x_index", config.probeXIndex},
            {"y_index", config.probeYIndex},
        }},
        {"propagation_distance_m", config.propagationDistanceMetres},
        {"psf_mtf", {
            {"focal_length_m", config.psfFocalLengthMetres},
            {"grid_resolution", config.psfGridResolution},
            {"mtf_maximum_cutoff_multiple", config.mtfMaximumCutoffMultiple},
            {"mtf_sample_count", config.mtfSampleCount},
            {"pupil_radius_m", config.psfPupilRadiusMetres},
            {"samples_per_first_dark_radius", config.psfSamplesPerFirstDarkRadius},
        }},
        {"requested_half_angle_rad", {
            config.requestedHalfAngleXRadians,
            config.requestedHalfAngleYRadians,
        }},
        {"spectrum_floor_db", config.spectrumFloorDecibels},
    };
}

[[nodiscard]] std::optional<double> optionalFinite(
    const Json& value,
    const char* context) {
    if (value.is_null()) {
        return std::nullopt;
    }
    return finiteNumber(value, context);
}

[[nodiscard]] samplingdebug::SamplingDebuggerConfig parseSamplingDebugger(
    const Json& json) {
    requireKeys(json, {
        "four_f", "illuminated_extent_m", "minimum_boundary_guard_samples",
        "probe", "propagation_distance_m", "psf_mtf",
        "requested_half_angle_rad", "spectrum_floor_db",
    }, "sampling debugger config");
    samplingdebug::SamplingDebuggerConfig config;
    config.propagationDistanceMetres = finiteNumber(
        json.at("propagation_distance_m"), "sampling propagation distance");
    const auto angles = numberPair(
        json.at("requested_half_angle_rad"), "requested half angle");
    config.requestedHalfAngleXRadians = angles.first;
    config.requestedHalfAngleYRadians = angles.second;
    const auto& extents = json.at("illuminated_extent_m");
    if (!extents.is_array() || extents.size() != 2U) {
        throw std::invalid_argument(
            "illuminated extent must contain X and Y values or nulls");
    }
    config.illuminatedExtentXMetres = optionalFinite(
        extents.at(0), "illuminated extent X");
    config.illuminatedExtentYMetres = optionalFinite(
        extents.at(1), "illuminated extent Y");
    config.minimumBoundaryGuardSamples = sizeValue(
        json.at("minimum_boundary_guard_samples"), "boundary guard samples");

    const auto& probe = json.at("probe");
    requireKeys(probe, {"distances_m", "x_index", "y_index"}, "sampling probe");
    config.probeXIndex = sizeValue(probe.at("x_index"), "probe X index");
    config.probeYIndex = sizeValue(probe.at("y_index"), "probe Y index");
    if (!probe.at("distances_m").is_array()) {
        throw std::invalid_argument("probe distances must be an array");
    }
    config.probeDistancesMetres.clear();
    for (const auto& value : probe.at("distances_m")) {
        config.probeDistancesMetres.push_back(
            finiteNumber(value, "probe distance"));
    }

    const auto& psf = json.at("psf_mtf");
    requireKeys(psf, {
        "focal_length_m", "grid_resolution", "mtf_maximum_cutoff_multiple",
        "mtf_sample_count", "pupil_radius_m", "samples_per_first_dark_radius",
    }, "PSF/MTF config");
    config.psfFocalLengthMetres = finiteNumber(
        psf.at("focal_length_m"), "PSF focal length");
    config.psfPupilRadiusMetres = finiteNumber(
        psf.at("pupil_radius_m"), "PSF pupil radius");
    config.psfGridResolution = sizeValue(
        psf.at("grid_resolution"), "PSF grid resolution");
    config.psfSamplesPerFirstDarkRadius = finiteNumber(
        psf.at("samples_per_first_dark_radius"), "PSF samples per dark radius");
    config.mtfSampleCount = sizeValue(
        psf.at("mtf_sample_count"), "MTF sample count");
    config.mtfMaximumCutoffMultiple = finiteNumber(
        psf.at("mtf_maximum_cutoff_multiple"), "MTF cutoff multiple");
    config.spectrumFloorDecibels = finiteNumber(
        json.at("spectrum_floor_db"), "spectrum floor");

    const auto& fourF = json.at("four_f");
    requireKeys(fourF, {
        "display_floor_db", "filter_inner_radius_m", "filter_kind",
        "filter_outer_radius_m", "first_focal_length_m", "second_focal_length_m",
    }, "4-f config");
    config.fourFFirstFocalLengthMetres = finiteNumber(
        fourF.at("first_focal_length_m"), "4-f first focal length");
    config.fourFSecondFocalLengthMetres = finiteNumber(
        fourF.at("second_focal_length_m"), "4-f second focal length");
    config.fourFFilterKind = parseFilterKind(fourF.at("filter_kind"));
    config.fourFFilterInnerRadiusMetres = finiteNumber(
        fourF.at("filter_inner_radius_m"), "4-f inner radius");
    config.fourFFilterOuterRadiusMetres = finiteNumber(
        fourF.at("filter_outer_radius_m"), "4-f outer radius");
    config.fourFDisplayFloorDecibels = finiteNumber(
        fourF.at("display_floor_db"), "4-f display floor");
    return config;
}

void validateWaveDetector(const wave::WaveDetectorConfig& config) {
    static_cast<void>(sourceKindName(config.sourceKind));
    static_cast<void>(apertureKindName(config.apertureKind));
    static_cast<void>(propagatorName(config.propagator));
    requirePositive(config.wavelengthMetres, "wave wavelength");
    requireFinite(config.sourceAmplitude.real(), "wave amplitude real part");
    requireFinite(config.sourceAmplitude.imag(), "wave amplitude imaginary part");
    requirePositive(config.gaussianWaistRadiusMetres, "Gaussian waist radius");
    requireFinite(config.planeWaveDirectionCosineX, "plane-wave direction X");
    requireFinite(config.planeWaveDirectionCosineY, "plane-wave direction Y");
    const double directionSquared = config.planeWaveDirectionCosineX
            * config.planeWaveDirectionCosineX
        + config.planeWaveDirectionCosineY * config.planeWaveDirectionCosineY;
    if (!std::isfinite(directionSquared) || directionSquared > 1.0) {
        throw std::invalid_argument("plane-wave transverse direction is not propagating");
    }
    requireFinite(config.sourcePhaseAtOriginRadians, "source phase");
    requirePositive(config.circularApertureRadiusMetres, "circular aperture radius");
    requirePositive(config.rectangularHalfWidthMetres, "rectangular half width");
    requirePositive(config.rectangularHalfHeightMetres, "rectangular half height");
    requirePositive(config.doubleSlitWidthMetres, "double-slit width");
    requirePositive(config.doubleSlitHeightMetres, "double-slit height");
    requirePositive(config.doubleSlitSeparationMetres, "double-slit separation");
    requireFinite(config.apertureCenterXMetres, "aperture center X");
    requireFinite(config.apertureCenterYMetres, "aperture center Y");
    requireFinite(config.thinLensFocalLengthMetres, "thin-lens focal length");
    if (std::abs(config.thinLensFocalLengthMetres) < 1e-6) {
        throw std::invalid_argument("thin-lens focal length magnitude is too small");
    }
    requireFinite(config.thinLensCenterXMetres, "thin-lens center X");
    requireFinite(config.thinLensCenterYMetres, "thin-lens center Y");
    requireFinite(config.propagationDistanceMetres, "wave propagation distance");
    if (config.propagationDistanceMetres < 0.0) {
        throw std::invalid_argument("wave propagation distance must be non-negative");
    }
    if (config.gridResolution < 2U
        || (config.gridResolution & (config.gridResolution - 1U)) != 0U
        || config.gridResolution
            > std::numeric_limits<std::size_t>::max() / config.gridResolution) {
        throw std::invalid_argument(
            "wave grid resolution must be a representable power of two");
    }
    requirePositive(config.gridPhysicalSpanMetres, "wave grid span");
    requirePositive(config.refractiveIndex, "wave refractive index");
}

void validateSamplingDebugger(
    const samplingdebug::SamplingDebuggerConfig& config,
    std::size_t waveResolution,
    double waveSpanMetres) {
    requireFinite(config.propagationDistanceMetres, "sampling propagation distance");
    requireFinite(config.requestedHalfAngleXRadians, "requested X half-angle");
    requireFinite(config.requestedHalfAngleYRadians, "requested Y half-angle");
    constexpr double kHalfPi = 1.57079632679489661923;
    if (config.requestedHalfAngleXRadians < 0.0
        || config.requestedHalfAngleXRadians >= kHalfPi
        || config.requestedHalfAngleYRadians < 0.0
        || config.requestedHalfAngleYRadians >= kHalfPi) {
        throw std::invalid_argument(
            "requested sampling half-angles must be in [0, pi/2)");
    }
    if (config.illuminatedExtentXMetres.has_value()
        != config.illuminatedExtentYMetres.has_value()) {
        throw std::invalid_argument(
            "sampling illuminated extents require both axes or neither");
    }
    if (config.illuminatedExtentXMetres.has_value()) {
        requirePositive(config.illuminatedExtentXMetres.value(),
            "illuminated extent X");
        requirePositive(config.illuminatedExtentYMetres.value(),
            "illuminated extent Y");
        if (config.illuminatedExtentXMetres.value() > waveSpanMetres
            || config.illuminatedExtentYMetres.value() > waveSpanMetres) {
            throw std::invalid_argument(
                "sampling illuminated extents must fit within the wave grid");
        }
    }
    if (config.probeXIndex >= waveResolution
        || config.probeYIndex >= waveResolution) {
        throw std::invalid_argument("sampling probe index is outside the wave grid");
    }
    if (config.probeDistancesMetres.empty()) {
        throw std::invalid_argument("sampling probe requires at least one distance");
    }
    for (const double distance : config.probeDistancesMetres) {
        requireFinite(distance, "sampling probe distance");
    }
    requirePositive(config.psfFocalLengthMetres, "PSF focal length");
    requirePositive(config.psfPupilRadiusMetres, "PSF pupil radius");
    if (config.psfGridResolution == 0U) {
        throw std::invalid_argument("PSF grid resolution must be positive");
    }
    requirePositive(config.psfSamplesPerFirstDarkRadius,
        "PSF samples per first dark radius");
    if (config.mtfSampleCount < 2U) {
        throw std::invalid_argument("MTF sample count must be at least two");
    }
    requirePositive(config.mtfMaximumCutoffMultiple, "MTF cutoff multiple");
    requireFinite(config.spectrumFloorDecibels, "spectrum floor");
    requireFinite(config.fourFDisplayFloorDecibels, "4-f display floor");
    if (config.spectrumFloorDecibels >= 0.0
        || config.fourFDisplayFloorDecibels >= 0.0) {
        throw std::invalid_argument("display floors must be negative decibels");
    }
    requirePositive(config.fourFFirstFocalLengthMetres, "4-f first focal length");
    requirePositive(config.fourFSecondFocalLengthMetres, "4-f second focal length");
    requireFinite(config.fourFFilterInnerRadiusMetres, "4-f inner radius");
    requireFinite(config.fourFFilterOuterRadiusMetres, "4-f outer radius");
    if (config.fourFFilterInnerRadiusMetres < 0.0) {
        throw std::invalid_argument("4-f inner radius must be non-negative");
    }
    requirePositive(config.fourFFilterOuterRadiusMetres, "4-f outer radius");
    switch (config.fourFFilterKind) {
    case compute::fourier::CircularFilterKind::PassAll:
        break;
    case compute::fourier::CircularFilterKind::LowPass:
        requirePositive(config.fourFFilterOuterRadiusMetres,
            "4-f low-pass outer radius");
        break;
    case compute::fourier::CircularFilterKind::HighPass:
        requirePositive(config.fourFFilterInnerRadiusMetres,
            "4-f high-pass inner radius");
        break;
    case compute::fourier::CircularFilterKind::BandPass:
        if (config.fourFFilterInnerRadiusMetres < 0.0
            || config.fourFFilterOuterRadiusMetres
                <= config.fourFFilterInnerRadiusMetres) {
            throw std::invalid_argument(
                "4-f band-pass radii must define a non-empty interval");
        }
        break;
    default:
        throw std::invalid_argument("unsupported 4-f filter kind");
    }
}

} // namespace

void validateWaveWorkbenchProject(
    const WaveWorkbenchProjectDocument& document) {
    if (document.formatVersion != kWaveWorkbenchFormatVersion) {
        throw std::invalid_argument(
            "unsupported wave workbench project format version");
    }
    if (document.name.empty()) {
        throw std::invalid_argument("wave workbench project name cannot be empty");
    }
    project::validateProjectProvenance(document.provenance);
    validateWaveDetector(document.waveDetector);
    validateSamplingDebugger(
        document.samplingDebugger,
        document.waveDetector.gridResolution,
        document.waveDetector.gridPhysicalSpanMetres);
}

std::string serializeWaveWorkbenchProjectJson(
    const WaveWorkbenchProjectDocument& document) {
    validateWaveWorkbenchProject(document);
    const Json json = {
        {"format_version", document.formatVersion},
        {"model", "wave_sampling_workbench"},
        {"name", document.name},
        {"provenance", provenanceJson(document.provenance)},
        {"sampling_debugger", samplingDebuggerJson(document.samplingDebugger)},
        {"wave_detector", waveDetectorJson(document.waveDetector)},
    };
    return json.dump(2) + "\n";
}

WaveWorkbenchProjectDocument deserializeWaveWorkbenchProjectJson(
    std::string_view jsonText) {
    try {
        const Json json = Json::parse(jsonText);
        requireKeys(json, {
            "format_version", "model", "name", "provenance",
            "sampling_debugger", "wave_detector",
        }, "wave workbench project");
        if (!json.at("format_version").is_number_integer()
            || json.at("format_version").get<int>()
                != kWaveWorkbenchFormatVersion) {
            throw std::invalid_argument(
                "unsupported wave workbench project format version");
        }
        if (!json.at("model").is_string()
            || json.at("model").get<std::string>()
                != "wave_sampling_workbench") {
            throw std::invalid_argument("unsupported wave workbench project model");
        }
        if (!json.at("name").is_string()
            || json.at("name").get<std::string>().empty()) {
            throw std::invalid_argument(
                "wave workbench project name must be a non-empty string");
        }
        WaveWorkbenchProjectDocument result {
            .formatVersion = kWaveWorkbenchFormatVersion,
            .name = json.at("name").get<std::string>(),
            .provenance = parseProvenance(json.at("provenance")),
            .waveDetector = parseWaveDetector(json.at("wave_detector")),
            .samplingDebugger = parseSamplingDebugger(
                json.at("sampling_debugger")),
        };
        validateWaveWorkbenchProject(result);
        return result;
    } catch (const nlohmann::json::exception& error) {
        throw std::invalid_argument(
            std::string("invalid wave workbench project JSON: ") + error.what());
    }
}

void saveWaveWorkbenchProject(
    const std::filesystem::path& path,
    const WaveWorkbenchProjectDocument& document) {
    const auto text = serializeWaveWorkbenchProjectJson(document);
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) {
        throw std::runtime_error(
            "cannot open wave workbench project for writing");
    }
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!stream) {
        throw std::runtime_error("failed to write wave workbench project");
    }
}

WaveWorkbenchProjectDocument loadWaveWorkbenchProject(
    const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error(
            "cannot open wave workbench project for reading");
    }
    const std::string text {
        std::istreambuf_iterator<char>(stream),
        std::istreambuf_iterator<char>(),
    };
    if (!stream.eof() && stream.fail()) {
        throw std::runtime_error("failed to read wave workbench project");
    }
    return deserializeWaveWorkbenchProjectJson(text);
}

} // namespace holobench::app::waveproject
