#include "optics/wave/BeamFollowingField.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <numbers>
#include <set>
#include <stdexcept>
#include <string_view>
#include <utility>

#include "compute/fft/IFftBackend.hpp"
#include "compute/propagation/AngularSpectrumPropagator.hpp"
#include "optics/ray/LensPrescriptionCatalog.hpp"
#include "optics/slm/SlmResponse.hpp"

namespace holobench::optics::wave {
namespace {

constexpr std::size_t kMaximumWorkingSamplesPerAxis = 4096U;
constexpr double kBoundaryEnvelopeThreshold = 1e-6;

struct EnvelopeSample final {
    double amplitude = 0.0;
};

const scene::BenchComponent& requireSource(
    const scene::BenchScene& bench,
    const scene::BeamState& terminalBeam) {
    if (terminalBeam.provenance.componentPath.empty()) {
        throw std::invalid_argument(
            "beam-following field has no source provenance");
    }
    const auto* source = bench.find(
        terminalBeam.provenance.componentPath.front());
    if (source == nullptr
        || (source->kind != scene::BenchComponentKind::LaserSource
            && source->kind
                != scene::BenchComponentKind::ObjectWavefrontSource)) {
        throw std::invalid_argument(
            "beam-following field does not begin at a supported source");
    }
    return *source;
}

const scene::BenchComponent& requireTarget(
    const scene::BenchScene& bench,
    const scene::BeamState& terminalBeam,
    std::span<const scene::BenchPathInteraction> path) {
    if (path.empty()
        || terminalBeam.provenance.componentPath.size() < 2U
        || path.back().componentId
            != terminalBeam.provenance.componentPath.back()) {
        throw std::invalid_argument(
            "beam-following field requires a complete ordered target path");
    }
    const auto* target = bench.find(path.back().componentId);
    if (target == nullptr
        || (target->kind != scene::BenchComponentKind::ScreenDetector
            && target->kind != scene::BenchComponentKind::FieldProbe
            && target->kind
                != scene::BenchComponentKind::HolographicPlate)) {
        throw std::invalid_argument(
            "beam-following field target is not an observation plane");
    }
    const auto& pathBeam = path.back().incidentBeam;
    if (pathBeam.provenance.branchId
            != terminalBeam.provenance.branchId
        || pathBeam.wavelengthMetres != terminalBeam.wavelengthMetres
        || pathBeam.coherenceId != terminalBeam.coherenceId) {
        throw std::invalid_argument(
            "beam-following target evidence does not match the terminal branch");
    }
    return *target;
}

void validateOptions(const BeamFollowingFieldOptions& options) {
    if (options.sampleWidth < 2U || options.sampleHeight < 2U
        || options.sampleWidth > kMaximumWorkingSamplesPerAxis / 2U
        || options.sampleHeight > kMaximumWorkingSamplesPerAxis / 2U) {
        throw std::invalid_argument(
            "beam-following output dimensions must each be in [2, 2048]");
    }
    if (!std::isfinite(options.extentWidthMetres)
        || !std::isfinite(options.extentHeightMetres)
        || options.extentWidthMetres <= 0.0
        || options.extentHeightMetres <= 0.0
        || !std::isfinite(options.centreXMetres)
        || !std::isfinite(options.centreYMetres)
        || !std::isfinite(options.refractiveIndex)
        || options.refractiveIndex <= 0.0
        || !std::isfinite(options.environmentTemperatureKelvin)
        || options.environmentTemperatureKelvin <= 0.0) {
        throw std::invalid_argument(
            "beam-following sampling geometry must be positive and finite");
    }
}

const scene::CalibrationAssetReference* placedSlmResponseReference(
    const scene::BenchComponent& component) {
    const scene::CalibrationAssetReference* result = nullptr;
    for (const auto& reference : component.instrument.calibrationAssets) {
        if (reference.kind != scene::CalibrationAssetKind::SlmResponse) {
            continue;
        }
        if (result != nullptr) {
            throw std::invalid_argument(
                "placed SLM has multiple response calibration references");
        }
        result = &reference;
    }
    return result;
}

const optics::slm::CalibratedSlmResponse* resolvePlacedSlmResponse(
    const scene::BenchComponent& component,
    double vacuumWavelengthMetres,
    const BeamFollowingFieldOptions& options,
    std::string& calibrationId) {
    const auto* reference = placedSlmResponseReference(component);
    if (reference == nullptr) {
        return nullptr;
    }
    if (component.instrument.calibrationMode
            != scene::InstrumentCalibrationMode::Calibrated
        || options.slmResponses == nullptr) {
        throw std::invalid_argument(
            "placed SLM response reference has no verified calibrated resolver");
    }
    if (!scene::isCalibrationAssetApplicable(
            *reference,
            component.instrument,
            vacuumWavelengthMetres,
            options.environmentTemperatureKelvin)) {
        throw std::invalid_argument(
            "placed SLM response is outside its wavelength or temperature validity domain");
    }
    const auto* response = options.slmResponses->resolveSlmResponse(
        reference->calibrationId);
    if (response == nullptr) {
        throw std::invalid_argument(
            "placed SLM response is absent from the verified resolver: "
            + reference->calibrationId);
    }
    // Evaluate one endpoint now so a reference whose declared validity has
    // drifted outside the parsed LUT fails before any field mutation.
    static_cast<void>(response->evaluate(vacuumWavelengthMetres, 0.0));
    calibrationId = reference->calibrationId;
    return response;
}

EnvelopeSample sourceEnvelope(
    const scene::BenchComponent& source,
    double beamXMetres,
    double beamYMetres) {
    if (source.kind == scene::BenchComponentKind::LaserSource) {
        const auto& parameters = std::get<scene::LaserSourceParameters>(
            source.parameters);
        const double radius = std::hypot(beamXMetres, beamYMetres);
        if (parameters.profile == scene::LaserBeamProfile::Collimated) {
            return {
                .amplitude = radius <= parameters.beamRadiusMetres ? 1.0 : 0.0,
            };
        }
        const double normalizedRadius = radius / parameters.beamRadiusMetres;
        return {
            .amplitude = std::exp(-normalizedRadius * normalizedRadius),
        };
    }
    const auto& parameters
        = std::get<scene::ObjectWavefrontSourceParameters>(source.parameters);
    const bool inside = std::abs(beamXMetres) <= 0.5 * parameters.widthMetres
        && std::abs(beamYMetres) <= 0.5 * parameters.heightMetres;
    return {.amplitude = inside ? 1.0 : 0.0};
}

double sourceNormalizationAreaSquareMetres(
    const scene::BenchComponent& source) {
    if (source.kind == scene::BenchComponentKind::LaserSource) {
        const auto& parameters = std::get<scene::LaserSourceParameters>(
            source.parameters);
        const double diskArea = std::numbers::pi
            * parameters.beamRadiusMetres * parameters.beamRadiusMetres;
        return parameters.profile == scene::LaserBeamProfile::Collimated
            ? diskArea
            : 0.5 * diskArea;
    }
    const auto& parameters
        = std::get<scene::ObjectWavefrontSourceParameters>(source.parameters);
    return parameters.widthMetres * parameters.heightMetres;
}

std::complex<double> finitePhasor(double phaseRadians) {
    if (!std::isfinite(phaseRadians)) {
        throw std::overflow_error(
            "beam-following field phase is not representable");
    }
    return std::polar(
        1.0,
        std::remainder(phaseRadians, 2.0 * std::numbers::pi));
}

bool approximatelyAligned(math::Vec3d first, math::Vec3d second) {
    constexpr double kAlignmentTolerance = 1e-10;
    return math::dot(math::normalized(first), math::normalized(second))
        >= 1.0 - kAlignmentTolerance;
}

bool approximatelyParallel(math::Vec3d first, math::Vec3d second) {
    constexpr double kAlignmentTolerance = 1e-10;
    return std::abs(math::dot(
        math::normalized(first), math::normalized(second)))
        >= 1.0 - kAlignmentTolerance;
}

const material::OpticalMaterial& requireLensMaterial(
    const ray::SequentialLensPrescription& prescription,
    const std::string& materialId) {
    const auto found = std::find_if(
        prescription.materials.begin(), prescription.materials.end(),
        [&](const auto& material) { return material.id == materialId; });
    if (found == prescription.materials.end()) {
        throw std::logic_error(
            "validated real-lens prescription lost a referenced material");
    }
    return *found;
}

ray::SequentialLensPrescription requireSupportedWaveLens(
    const scene::BenchComponent& component,
    const scene::BenchPathInteraction& interaction,
    math::Vec3d pathDirection,
    double wavelengthMetres,
    const ray::ILensPrescriptionResolver* lensPrescriptions) {
    const auto& parameters
        = std::get<scene::RealLensAssemblyParameters>(component.parameters);
    if (lensPrescriptions == nullptr) {
        throw std::invalid_argument(
            "real-lens wave propagation requires a prescription resolver");
    }
    const auto* prescription = lensPrescriptions->resolve(
        parameters.prescriptionId);
    if (prescription == nullptr) {
        throw std::invalid_argument(
            "real-lens prescription '" + parameters.prescriptionId
            + "' is absent from the wave-propagation catalog");
    }
    auto placed = ray::placeLensPrescription(
        *prescription, component.transform);
    if (!approximatelyAligned(
            component.transform.localZAxisInWorld, pathDirection)) {
        throw std::invalid_argument(
            "real-lens scalar wave adapter requires forward coaxial incidence");
    }
    const double centreOffset = math::length(
        interaction.hitPointMetres
        - component.transform.translationMetres);
    if (centreOffset > 1e-9) {
        throw std::invalid_argument(
            "real-lens scalar wave adapter requires the centre ray to pass through the prescription axis");
    }

    const auto& firstFrame = placed.surfaces.front().localToWorld;
    double previousZ = -1.0;
    constexpr double kFrameToleranceMetres = 2e-12;
    constexpr double kMaximumParaxialSurfaceSlope = 0.25;
    const double assemblyRadius
        = 0.5 * parameters.clearApertureDiameterMetres;
    for (const auto& surface : placed.surfaces) {
        const auto relativeVertex = math::transformPointWorldToLocal(
            firstFrame, surface.localToWorld.translationMetres);
        if (std::abs(relativeVertex.x) > kFrameToleranceMetres
            || std::abs(relativeVertex.y) > kFrameToleranceMetres
            || relativeVertex.z <= previousZ) {
            throw std::invalid_argument(
                "real-lens scalar wave adapter requires strictly ordered coaxial surface vertices");
        }
        if (!approximatelyAligned(
                surface.localToWorld.localZAxisInWorld,
                firstFrame.localZAxisInWorld)) {
            throw std::invalid_argument(
                "real-lens scalar wave adapter does not support tilted or decentered surfaces");
        }
        const double evaluatedRadius = std::min(
            assemblyRadius, surface.geometry.clearSemiDiameterMetres);
        const auto edge = ray::evaluateSurfaceSag(
            surface.geometry, evaluatedRadius);
        if (std::abs(edge.radialDerivative)
            > kMaximumParaxialSurfaceSlope) {
            throw std::invalid_argument(
                "real-lens prescription exceeds the validated low-NA scalar surface-slope limit");
        }
        previousZ = relativeVertex.z;
    }
    const double entryIndex = material::refractiveIndexAtVacuumWavelength(
        requireLensMaterial(
            placed, placed.surfaces.front().materialBeforeId),
        wavelengthMetres);
    const double exitIndex = material::refractiveIndexAtVacuumWavelength(
        requireLensMaterial(
            placed, placed.surfaces.back().materialAfterId),
        wavelengthMetres);
    constexpr double kAmbientIndexTolerance = 2e-12;
    if (std::abs(entryIndex - 1.0) > kAmbientIndexTolerance
        || std::abs(exitIndex - 1.0) > kAmbientIndexTolerance) {
        throw std::invalid_argument(
            "real-lens scalar wave adapter requires vacuum/air-equivalent entry and exit media");
    }
    return placed;
}

void validateSupportedPath(
    const scene::BenchScene& bench,
    math::Vec3d initialDirection,
    std::span<const scene::BenchPathInteraction> path,
    const ray::ILensPrescriptionResolver* lensPrescriptions) {
    math::Vec3d pathDirection = math::normalized(initialDirection);
    for (const auto& interaction : path) {
        const auto* component = bench.find(interaction.componentId);
        if (component == nullptr) {
            throw std::invalid_argument(
                "beam-following path contains a missing component");
        }
        if (!approximatelyAligned(
                interaction.incidentBeam.direction, pathDirection)) {
            throw std::invalid_argument(
                "beam-following interaction directions are not connected in path order");
        }
        if (requiresBeamFollowingWaveTransform(component->kind)) {
            const double incidenceCosine = std::abs(math::dot(
                math::normalized(pathDirection),
                component->transform.localZAxisInWorld));
            if (incidenceCosine <= 1e-6) {
                throw std::invalid_argument(
                    "beam-following path grazes a local optical plane");
            }
            if (component->kind
                    == scene::BenchComponentKind::IdealThinLens
                && !approximatelyParallel(
                    component->transform.localZAxisInWorld,
                    pathDirection)) {
                throw std::invalid_argument(
                    "an ideal powered lens is tilted relative to its incident centre ray");
            }
            if (component->kind
                == scene::BenchComponentKind::RealLensAssembly) {
                static_cast<void>(requireSupportedWaveLens(
                    *component,
                    interaction,
                    pathDirection,
                    interaction.incidentBeam.wavelengthMetres,
                    lensPrescriptions));
            }
        }
        if (interaction.hasOutgoingBeam) {
            const auto outgoing = math::normalized(
                interaction.outgoingBeam.direction);
            if (!approximatelyAligned(outgoing, pathDirection)
                && component->kind
                    != scene::BenchComponentKind::PlanarMirror
                && component->kind
                    != scene::BenchComponentKind::BeamSplitterCombiner) {
                throw std::invalid_argument(
                    "a powered or unsupported component changes the centre-ray direction");
            }
            pathDirection = outgoing;
        }
    }
}

void propagateInRefractiveIndex(
    field::ComplexField2D& value,
    double refractiveIndex,
    double distanceMetres,
    compute::propagation::AngularSpectrumPropagator& propagator) {
    field::ComplexField2D mediumField(
        value.width(),
        value.height(),
        value.pitchXMetres(),
        value.pitchYMetres(),
        value.vacuumWavelengthMetres(),
        refractiveIndex);
    std::copy(
        value.samples().begin(), value.samples().end(),
        mediumField.samples().begin());
    static_cast<void>(propagator.propagateInPlace(
        mediumField, distanceMetres));
    std::copy(
        mediumField.samples().begin(), mediumField.samples().end(),
        value.samples().begin());
}

void applySequentialLens(
    field::ComplexField2D& value,
    const scene::BenchComponent& component,
    const scene::BenchPathInteraction& interaction,
    math::Vec3d propagationDirection,
    const ray::ILensPrescriptionResolver* lensPrescriptions,
    compute::propagation::AngularSpectrumPropagator& propagator,
    BeamFollowingFieldDiagnostics& diagnostics) {
    const auto& parameters
        = std::get<scene::RealLensAssemblyParameters>(component.parameters);
    const auto placed = requireSupportedWaveLens(
        component,
        interaction,
        propagationDirection,
        value.vacuumWavelengthMetres(),
        lensPrescriptions);
    const auto& firstFrame = placed.surfaces.front().localToWorld;
    const double vacuumWavenumber
        = 2.0 * std::numbers::pi / value.vacuumWavelengthMetres();
    const double assemblyRadius
        = 0.5 * parameters.clearApertureDiameterMetres;
    for (std::size_t surfaceIndex = 0;
         surfaceIndex < placed.surfaces.size(); ++surfaceIndex) {
        const auto& surface = placed.surfaces[surfaceIndex];
        const double beforeIndex
            = material::refractiveIndexAtVacuumWavelength(
                requireLensMaterial(
                    placed, surface.materialBeforeId),
                value.vacuumWavelengthMetres());
        const double afterIndex
            = material::refractiveIndexAtVacuumWavelength(
                requireLensMaterial(
                    placed, surface.materialAfterId),
                value.vacuumWavelengthMetres());
        const double clearRadius = std::min(
            assemblyRadius, surface.geometry.clearSemiDiameterMetres);
        for (std::size_t y = 0; y < value.height(); ++y) {
            for (std::size_t x = 0; x < value.width(); ++x) {
                const double radius = std::hypot(
                    value.xCoordinateMetres(x),
                    value.yCoordinateMetres(y));
                if (radius > clearRadius) {
                    value.at(x, y) = {0.0, 0.0};
                    continue;
                }
                const double sag = ray::evaluateSurfaceSag(
                    surface.geometry, radius).sagMetres;
                const double phase = vacuumWavenumber
                    * (beforeIndex - afterIndex) * sag;
                value.at(x, y) *= finitePhasor(phase);
            }
        }
        if (surfaceIndex + 1U < placed.surfaces.size()) {
            const auto currentVertex = math::transformPointWorldToLocal(
                firstFrame, surface.localToWorld.translationMetres);
            const auto nextVertex = math::transformPointWorldToLocal(
                firstFrame,
                placed.surfaces[surfaceIndex + 1U]
                    .localToWorld.translationMetres);
            propagateInRefractiveIndex(
                value,
                afterIndex,
                nextVertex.z - currentVertex.z,
                propagator);
            ++diagnostics.propagatedSegmentCount;
        }
    }
    diagnostics.appliedWaveComponentIds.push_back(component.id);
    diagnostics.appliedRealLensPrescriptionIds.push_back(
        parameters.prescriptionId);
    diagnostics.warnings.push_back(
        component.id + ": prescription '" + parameters.prescriptionId
        + "' used a scalar low-NA split-step surface-phase model; exact sequential centre-ray routing remains authoritative, while Fresnel loss, polarization, high-NA longitudinal fields, and non-sequential ghosts are excluded");
}

math::Vec3d reflectVector(math::Vec3d value, math::Vec3d normal) {
    return value - normal * (2.0 * math::dot(value, normal));
}

math::Vec3d projectedLocalPoint(
    const math::RigidTransform3d& fieldFrame,
    const scene::BenchComponent& component,
    math::Vec3d propagationDirection,
    double xMetres,
    double yMetres) {
    const math::Vec3d point = fieldFrame.translationMetres
        + fieldFrame.localXAxisInWorld * xMetres
        + fieldFrame.localYAxisInWorld * yMetres;
    const double denominator = math::dot(
        propagationDirection, component.transform.localZAxisInWorld);
    if (!std::isfinite(denominator) || std::abs(denominator) <= 1e-6) {
        throw std::invalid_argument(
            "local field cannot be projected onto a grazing optical plane");
    }
    const double distance = math::dot(
        component.transform.translationMetres - point,
        component.transform.localZAxisInWorld) / denominator;
    if (!std::isfinite(distance)) {
        throw std::overflow_error(
            "local field projection distance is not representable");
    }
    return math::transformPointWorldToLocal(
        component.transform, point + propagationDirection * distance);
}

struct SlmPixelLocation final {
    bool active = false;
    std::size_t column = 0;
    std::size_t row = 0;
};

SlmPixelLocation locateSlmPixel(
    const scene::SpatialLightModulatorParameters& parameters,
    double localXMetres,
    double localYMetres) {
    const double pitchX = parameters.widthMetres
        / static_cast<double>(parameters.pixelWidth);
    const double pitchY = parameters.heightMetres
        / static_cast<double>(parameters.pixelHeight);
    const double gridX = (localXMetres + 0.5 * parameters.widthMetres) / pitchX;
    const double gridY = (localYMetres + 0.5 * parameters.heightMetres) / pitchY;
    if (gridX < 0.0 || gridY < 0.0
        || gridX >= static_cast<double>(parameters.pixelWidth)
        || gridY >= static_cast<double>(parameters.pixelHeight)) {
        return {};
    }
    const auto column = static_cast<std::size_t>(std::floor(gridX));
    const auto row = static_cast<std::size_t>(std::floor(gridY));
    const double pixelX = gridX - std::floor(gridX) - 0.5;
    const double pixelY = gridY - std::floor(gridY) - 0.5;
    return {
        .active = std::abs(pixelX) < 0.5 * parameters.fillFactor
            && std::abs(pixelY) < 0.5 * parameters.fillFactor,
        .column = column,
        .row = row,
    };
}

std::string_view slmPatternName(scene::SlmCommandPattern pattern) noexcept {
    switch (pattern) {
    case scene::SlmCommandPattern::Uniform: return "uniform";
    case scene::SlmCommandPattern::LinearRamp: return "linear ramp";
    case scene::SlmCommandPattern::Checkerboard: return "checkerboard";
    }
    return "unknown";
}

std::string_view slmOriginName(scene::SlmCommandOrigin origin) noexcept {
    switch (origin) {
    case scene::SlmCommandOrigin::Manual: return "manual";
    case scene::SlmCommandOrigin::Automation: return "automation";
    }
    return "unknown";
}

const PlacedSlmSparseCommand* sparseCommandFor(
    std::span<const PlacedSlmSparseCommand> commands,
    std::string_view componentId) {
    const auto found = std::find_if(
        commands.begin(), commands.end(), [&](const auto& command) {
            return command.componentId == componentId;
        });
    return found == commands.end() ? nullptr : &*found;
}

void validateSparseSlmCommands(
    const scene::BenchScene& bench,
    std::span<const PlacedSlmSparseCommand> commands) {
    std::set<std::string> componentIds;
    for (const auto& command : commands) {
        const auto* component = bench.find(command.componentId);
        if (!scene::isStableBenchId(command.componentId)
            || !scene::isStableBenchId(command.commandId)
            || !componentIds.insert(command.componentId).second
            || component == nullptr
            || component->kind
                != scene::BenchComponentKind::SpatialLightModulator) {
            throw std::invalid_argument(
                "placed sparse SLM command component identity is invalid");
        }
        const auto& parameters
            = std::get<scene::SpatialLightModulatorParameters>(
                component->parameters);
        if (command.commandId != parameters.commandId
            || command.pixelWidth != parameters.pixelWidth
            || command.pixelHeight != parameters.pixelHeight
            || !std::isfinite(command.defaultNormalizedCommand)
            || command.defaultNormalizedCommand < 0.0
            || command.defaultNormalizedCommand > 1.0) {
            throw std::invalid_argument(
                "placed sparse SLM command does not match its component");
        }
        if ((command.calibratedResponse == nullptr)
                != command.calibrationId.empty()
            || (!command.calibrationId.empty()
                && !scene::isStableBenchId(command.calibrationId))) {
            throw std::invalid_argument(
                "placed sparse SLM calibration identity is invalid");
        }
        std::pair<std::size_t, std::size_t> previous {};
        bool first = true;
        for (const auto& pixel : command.pixels) {
            const std::pair coordinate {pixel.row, pixel.column};
            if (pixel.column >= command.pixelWidth
                || pixel.row >= command.pixelHeight
                || !std::isfinite(pixel.normalizedCommand)
                || pixel.normalizedCommand < 0.0
                || pixel.normalizedCommand > 1.0
                || (!first && coordinate <= previous)) {
                throw std::invalid_argument(
                    "placed sparse SLM pixels must be unique canonical normalized coordinates");
            }
            previous = coordinate;
            first = false;
        }
    }
}

double evaluateSparseCommand(
    const PlacedSlmSparseCommand& command,
    std::size_t column,
    std::size_t row) {
    const std::pair coordinate {row, column};
    const auto found = std::lower_bound(
        command.pixels.begin(), command.pixels.end(), coordinate,
        [](const auto& pixel, const auto& target) {
            return std::pair {pixel.row, pixel.column} < target;
        });
    if (found != command.pixels.end()
        && found->row == row && found->column == column) {
        return found->normalizedCommand;
    }
    return command.defaultNormalizedCommand;
}

void applyProjectedElement(
    field::ComplexField2D& value,
    const scene::BenchComponent& component,
    const math::RigidTransform3d& fieldFrame,
    math::Vec3d propagationDirection,
    BeamFollowingFieldDiagnostics& diagnostics,
    std::span<const PlacedSlmSparseCommand> slmCommands,
    const BeamFollowingFieldOptions& options) {
    if (!requiresBeamFollowingWaveTransform(component.kind)
        || component.kind
            == scene::BenchComponentKind::RealLensAssembly) {
        return;
    }
    const double incidenceCosine = std::abs(math::dot(
        math::normalized(propagationDirection),
        component.transform.localZAxisInWorld));
    if (incidenceCosine < 1.0 - 1e-10) {
        diagnostics.usedTiltedElementProjection = true;
    }
    auto transformed = value;
    const double phaseCoefficient = component.kind
            == scene::BenchComponentKind::IdealThinLens
        ? (-0.5 * value.mediumWavenumberRadiansPerMetre())
            / std::get<scene::IdealThinLensParameters>(component.parameters)
                .focalLengthMetres
        : 0.0;
    const auto* sparse = component.kind
            == scene::BenchComponentKind::SpatialLightModulator
        ? sparseCommandFor(slmCommands, component.id)
        : nullptr;
    std::string placedCalibrationId;
    const auto* placedResponse = component.kind
            == scene::BenchComponentKind::SpatialLightModulator
        ? resolvePlacedSlmResponse(
            component,
            value.vacuumWavelengthMetres(),
            options,
            placedCalibrationId)
        : nullptr;
    if (placedResponse != nullptr && sparse != nullptr
        && sparse->calibratedResponse != nullptr) {
        throw std::invalid_argument(
            "placed and transient SLM response calibrations conflict for component "
            + component.id);
    }
    for (std::size_t y = 0; y < transformed.height(); ++y) {
        for (std::size_t x = 0; x < transformed.width(); ++x) {
            const auto local = projectedLocalPoint(
                fieldFrame,
                component,
                propagationDirection,
                transformed.xCoordinateMetres(x),
                transformed.yCoordinateMetres(y));
            bool transmitted = true;
            double phase = 0.0;
            double amplitudeTransmission = 1.0;
            switch (component.kind) {
            case scene::BenchComponentKind::PlanarMirror: {
                const auto& p = std::get<scene::PlanarMirrorParameters>(
                    component.parameters);
                transmitted = std::abs(local.x) <= 0.5 * p.widthMetres
                    && std::abs(local.y) <= 0.5 * p.heightMetres;
                break;
            }
            case scene::BenchComponentKind::BeamSplitterCombiner: {
                const auto& p = std::get<scene::BeamSplitterParameters>(
                    component.parameters);
                transmitted = std::abs(local.x) <= 0.5 * p.widthMetres
                    && std::abs(local.y) <= 0.5 * p.heightMetres;
                break;
            }
            case scene::BenchComponentKind::IdealThinLens: {
                const auto& p = std::get<scene::IdealThinLensParameters>(
                    component.parameters);
                transmitted = std::hypot(local.x, local.y)
                    <= 0.5 * p.clearApertureDiameterMetres;
                phase = phaseCoefficient
                    * (local.x * local.x + local.y * local.y);
                break;
            }
            case scene::BenchComponentKind::Aperture: {
                const auto& p = std::get<scene::ApertureParameters>(
                    component.parameters);
                if (p.shape == scene::ApertureShape::Circular) {
                    const double nx = local.x / (0.5 * p.widthMetres);
                    const double ny = local.y / (0.5 * p.heightMetres);
                    transmitted = nx * nx + ny * ny <= 1.0;
                } else if (p.shape == scene::ApertureShape::DoubleSlit) {
                    const double halfSeparation
                        = 0.5 * p.slitSeparationMetres;
                    transmitted = std::abs(
                            std::abs(local.x) - halfSeparation)
                            <= 0.5 * p.slitWidthMetres
                        && std::abs(local.y)
                            <= 0.5 * p.slitHeightMetres;
                } else {
                    transmitted = std::abs(local.x) <= 0.5 * p.widthMetres
                        && std::abs(local.y) <= 0.5 * p.heightMetres;
                }
                break;
            }
            case scene::BenchComponentKind::SpatialFilter: {
                const auto& p = std::get<scene::SpatialFilterParameters>(
                    component.parameters);
                transmitted = std::hypot(local.x, local.y)
                    <= 0.5 * p.pinholeDiameterMetres;
                break;
            }
            case scene::BenchComponentKind::SpatialLightModulator: {
                const auto& p
                    = std::get<scene::SpatialLightModulatorParameters>(
                        component.parameters);
                const auto location = locateSlmPixel(p, local.x, local.y);
                transmitted = location.active;
                if (transmitted) {
                    const double command = sparse == nullptr
                        ? scene::evaluateSlmNormalizedCommand(
                            p, location.column, location.row)
                        : evaluateSparseCommand(
                            *sparse, location.column, location.row);
                    const auto* response = placedResponse != nullptr
                        ? placedResponse
                        : sparse != nullptr
                            ? sparse->calibratedResponse
                            : nullptr;
                    if (response != nullptr) {
                        const auto evaluated = response->evaluate(
                                value.vacuumWavelengthMetres(), command);
                        amplitudeTransmission
                            = evaluated.amplitudeTransmission;
                        phase = evaluated.unwrappedPhaseDelayRadians;
                    } else if (p.modulationMode
                        == scene::SlmModulationMode::Amplitude) {
                        amplitudeTransmission = command;
                    } else {
                        phase = command * p.phaseRangeRadians;
                    }
                }
                break;
            }
            default:
                break;
            }
            if (!transmitted) {
                transformed.at(x, y) = {0.0, 0.0};
            } else {
                if (!std::isfinite(phase)) {
                    throw std::overflow_error(
                        "projected thin-element phase is not representable");
                }
                transformed.at(x, y) *= amplitudeTransmission
                    * finitePhasor(phase);
            }
        }
    }
    value = std::move(transformed);
    if (component.kind == scene::BenchComponentKind::SpatialFilter) {
        diagnostics.warnings.push_back(
            component.id
            + ": modeled as its explicit pinhole plane; the compound focusing objective requires separately placed lenses");
    } else if (component.kind
        == scene::BenchComponentKind::SpatialLightModulator) {
        const auto& parameters
            = std::get<scene::SpatialLightModulatorParameters>(
                component.parameters);
        diagnostics.warnings.push_back(
            component.id + ": applied "
            + (sparse == nullptr
                ? std::string(slmPatternName(parameters.commandPattern))
                : std::string("sparse raster"))
            + " command " + parameters.commandId + " from "
            + std::string(slmOriginName(parameters.commandOrigin))
            + " provenance");
        diagnostics.appliedSlmCommandIds.push_back(parameters.commandId);
        if (placedResponse != nullptr) {
            diagnostics.appliedSlmCalibrationIds.push_back(
                placedCalibrationId);
        } else if (sparse != nullptr
            && sparse->calibratedResponse != nullptr) {
            diagnostics.appliedSlmCalibrationIds.push_back(
                sparse->calibrationId);
        }
    }
    diagnostics.appliedWaveComponentIds.push_back(component.id);
}

void reflectFieldFrameAtFold(
    field::ComplexField2D& value,
    math::RigidTransform3d& fieldFrame,
    math::Vec3d mirrorNormal,
    math::Vec3d outgoingDirection) {
    auto reflected = value;
    for (std::size_t y = 0; y < value.height(); ++y) {
        for (std::size_t x = 0; x < value.width(); ++x) {
            reflected.at(x, y) = value.at(
                (value.width() - x) % value.width(), y);
        }
    }
    value = std::move(reflected);
    fieldFrame.localXAxisInWorld = -1.0 * reflectVector(
        fieldFrame.localXAxisInWorld, mirrorNormal);
    fieldFrame.localYAxisInWorld = reflectVector(
        fieldFrame.localYAxisInWorld, mirrorNormal);
    fieldFrame.localZAxisInWorld = math::normalized(outgoingDirection);
    math::validateRigidTransform(fieldFrame);
}

std::complex<double> bilinearSample(
    const field::ComplexField2D& value,
    double xMetres,
    double yMetres) {
    const double xIndex = xMetres / value.pitchXMetres()
        + static_cast<double>(value.width() / 2U);
    const double yIndex = yMetres / value.pitchYMetres()
        + static_cast<double>(value.height() / 2U);
    if (!std::isfinite(xIndex) || !std::isfinite(yIndex)) {
        throw std::overflow_error(
            "target tangent-plane sample coordinate is not representable");
    }
    const auto x0 = static_cast<long long>(std::floor(xIndex));
    const auto y0 = static_cast<long long>(std::floor(yIndex));
    const double tx = xIndex - static_cast<double>(x0);
    const double ty = yIndex - static_cast<double>(y0);
    const auto sample = [&](long long x, long long y) {
        if (x < 0 || y < 0
            || x >= static_cast<long long>(value.width())
            || y >= static_cast<long long>(value.height())) {
            return std::complex<double> {};
        }
        return value.at(
            static_cast<std::size_t>(x), static_cast<std::size_t>(y));
    };
    return (1.0 - tx) * (1.0 - ty) * sample(x0, y0)
        + tx * (1.0 - ty) * sample(x0 + 1, y0)
        + (1.0 - tx) * ty * sample(x0, y0 + 1)
        + tx * ty * sample(x0 + 1, y0 + 1);
}

field::ComplexField2D sampleOnTargetTangentPlane(
    const field::ComplexField2D& envelope,
    const math::RigidTransform3d& fieldFrame,
    math::Vec3d propagationDirection,
    const scene::BenchComponent& target,
    const BeamFollowingFieldOptions& options,
    BeamFollowingFieldDiagnostics& diagnostics) {
    field::ComplexField2D result(
        options.sampleWidth,
        options.sampleHeight,
        options.extentWidthMetres / static_cast<double>(options.sampleWidth),
        options.extentHeightMetres / static_cast<double>(options.sampleHeight),
        envelope.vacuumWavelengthMetres(),
        envelope.refractiveIndex());
    const double alignment = std::abs(math::dot(
        math::normalized(propagationDirection),
        target.transform.localZAxisInWorld));
    diagnostics.usedTargetTangentProjection = alignment < 1.0 - 1e-10;
    for (std::size_t y = 0; y < result.height(); ++y) {
        for (std::size_t x = 0; x < result.width(); ++x) {
            const math::Vec3d localPoint {
                result.xCoordinateMetres(x) + options.centreXMetres,
                result.yCoordinateMetres(y) + options.centreYMetres,
                0.0,
            };
            const math::Vec3d worldPoint = math::transformPointLocalToWorld(
                target.transform, localPoint);
            const math::Vec3d displacement
                = worldPoint - fieldFrame.translationMetres;
            const double transverseX = math::dot(
                displacement, fieldFrame.localXAxisInWorld);
            const double transverseY = math::dot(
                displacement, fieldFrame.localYAxisInWorld);
            const double longitudinal = math::dot(
                displacement, propagationDirection);
            result.at(x, y) = bilinearSample(
                envelope, transverseX, transverseY)
                * finitePhasor(
                    envelope.mediumWavenumberRadiansPerMetre()
                    * longitudinal);
        }
    }
    return result;
}

bool isBoundarySample(
    std::size_t x,
    std::size_t y,
    std::size_t width,
    std::size_t height) noexcept {
    return x == 0U || y == 0U || x + 1U == width || y + 1U == height;
}

void validateFiniteField(const field::ComplexField2D& value) {
    for (const auto& sample : value.samples()) {
        if (!std::isfinite(sample.real()) || !std::isfinite(sample.imag())) {
            throw std::invalid_argument(
                "derived beam-following source field must be finite");
        }
    }
}

field::ComplexField2D padCentered(
    const field::ComplexField2D& input) {
    field::ComplexField2D result(
        input.width() * 2U,
        input.height() * 2U,
        input.pitchXMetres(),
        input.pitchYMetres(),
        input.vacuumWavelengthMetres(),
        input.refractiveIndex());
    const std::size_t offsetX = input.width() / 2U;
    const std::size_t offsetY = input.height() / 2U;
    for (std::size_t y = 0; y < input.height(); ++y) {
        for (std::size_t x = 0; x < input.width(); ++x) {
            result.at(x + offsetX, y + offsetY) = input.at(x, y);
        }
    }
    return result;
}

BeamFollowingFieldResult propagatePreparedField(
    const scene::BenchScene& bench,
    field::ComplexField2D propagated,
    math::Vec3d previousPoint,
    math::Vec3d propagationDirection,
    math::RigidTransform3d fieldFrame,
    const scene::BenchComponent& target,
    std::span<const scene::BenchPathInteraction> pathInteractions,
    const BeamFollowingFieldOptions& options,
    compute::fft::IFftBackend& fftBackend,
    std::span<const PlacedSlmSparseCommand> slmCommands,
    const ray::ILensPrescriptionResolver* lensPrescriptions,
    double boundaryReferenceIntensity) {
    BeamFollowingFieldDiagnostics diagnostics;
    diagnostics.workingSampleWidth = propagated.width();
    diagnostics.workingSampleHeight = propagated.height();
    compute::propagation::AngularSpectrumPropagator propagator(fftBackend);
    for (const auto& interaction : pathInteractions) {
        const double distance = math::length(
            interaction.hitPointMetres - previousPoint);
        static_cast<void>(propagator.propagateInPlace(propagated, distance));
        ++diagnostics.propagatedSegmentCount;
        const auto* component = bench.find(interaction.componentId);
        if (component == nullptr) {
            throw std::logic_error(
                "beam-following path component disappeared during sampling");
        }
        fieldFrame.translationMetres = interaction.hitPointMetres;
        if (component->id != target.id) {
            if (component->kind
                == scene::BenchComponentKind::RealLensAssembly) {
                applySequentialLens(
                    propagated,
                    *component,
                    interaction,
                    propagationDirection,
                    lensPrescriptions,
                    propagator,
                    diagnostics);
            } else {
                applyProjectedElement(
                    propagated,
                    *component,
                    fieldFrame,
                    propagationDirection,
                    diagnostics,
                    slmCommands,
                    options);
            }
        }
        if (interaction.hasOutgoingBeam) {
            const math::Vec3d outgoing = math::normalized(
                interaction.outgoingBeam.direction);
            if (!approximatelyAligned(outgoing, propagationDirection)) {
                reflectFieldFrameAtFold(
                    propagated,
                    fieldFrame,
                    component->transform.localZAxisInWorld,
                    outgoing);
                diagnostics.usedFoldedPath = true;
                diagnostics.foldedWaveComponentIds.push_back(component->id);
            }
            propagationDirection = outgoing;
        }
        if (component->kind
                == scene::BenchComponentKind::RealLensAssembly
            && interaction.hasOutgoingBeam) {
            previousPoint = interaction.outgoingBeam.originMetres;
            fieldFrame.translationMetres = previousPoint;
        } else {
            previousPoint = interaction.hitPointMetres;
        }
    }

    auto sampled = sampleOnTargetTangentPlane(
        propagated,
        fieldFrame,
        propagationDirection,
        target,
        options,
        diagnostics);
    const double boundaryThresholdSquared
        = kBoundaryEnvelopeThreshold * kBoundaryEnvelopeThreshold
        * boundaryReferenceIntensity;
    for (std::size_t y = 0; y < sampled.height(); ++y) {
        for (std::size_t x = 0; x < sampled.width(); ++x) {
            if (isBoundarySample(x, y, sampled.width(), sampled.height())
                && std::norm(sampled.at(x, y))
                    > boundaryThresholdSquared) {
                diagnostics.supportTouchesBoundary = true;
            }
        }
    }
    if (diagnostics.supportTouchesBoundary) {
        diagnostics.warnings.emplace_back(
            "propagated field reaches the sampled boundary; periodic FFT wrap may contaminate this window");
    }
    if (diagnostics.usedTiltedElementProjection) {
        diagnostics.warnings.emplace_back(
            "tilted zero-thickness element footprints use carrier-tracked beam-normal projection; vector, polarization, thickness, and high-NA longitudinal effects are not modeled");
    }
    if (diagnostics.usedTargetTangentProjection) {
        diagnostics.warnings.emplace_back(
            "the oblique target tangent projection restores the exact centre carrier but treats envelope evolution across the tilted window paraxially");
    }
    return {
        .fieldAtTarget = std::move(sampled),
        .diagnostics = std::move(diagnostics),
    };
}

} // namespace

bool requiresBeamFollowingWaveTransform(
    scene::BenchComponentKind kind) noexcept {
    switch (kind) {
    case scene::BenchComponentKind::PlanarMirror:
    case scene::BenchComponentKind::BeamSplitterCombiner:
    case scene::BenchComponentKind::IdealThinLens:
    case scene::BenchComponentKind::RealLensAssembly:
    case scene::BenchComponentKind::Aperture:
    case scene::BenchComponentKind::SpatialFilter:
    case scene::BenchComponentKind::SpatialLightModulator:
        return true;
    default:
        return false;
    }
}

void validateBeamFollowingFieldPath(
    const scene::BenchScene& bench,
    const scene::BeamState& terminalBeam,
    std::span<const scene::BenchPathInteraction> pathInteractions,
    const ray::ILensPrescriptionResolver* lensPrescriptions) {
    scene::validateBeamState(terminalBeam);
    const auto& source = requireSource(bench, terminalBeam);
    static_cast<void>(requireTarget(
        bench, terminalBeam, pathInteractions));
    validateSupportedPath(
        bench,
        source.transform.localZAxisInWorld,
        pathInteractions,
        lensPrescriptions);
}

BeamFollowingFieldResult sampleBeamFollowingField(
    const scene::BenchScene& bench,
    const scene::BeamState& terminalBeam,
    std::span<const scene::BenchPathInteraction> pathInteractions,
    const BeamFollowingFieldOptions& options,
    compute::fft::IFftBackend& fftBackend,
    std::span<const PlacedSlmSparseCommand> slmCommands,
    const ray::ILensPrescriptionResolver* lensPrescriptions) {
    validateOptions(options);
    validateSparseSlmCommands(bench, slmCommands);
    validateBeamFollowingFieldPath(
        bench, terminalBeam, pathInteractions, lensPrescriptions);
    const auto& source = requireSource(bench, terminalBeam);
    const auto& target = requireTarget(
        bench, terminalBeam, pathInteractions);
    field::ComplexField2D propagated(
        options.sampleWidth * 2U,
        options.sampleHeight * 2U,
        options.extentWidthMetres / static_cast<double>(options.sampleWidth),
        options.extentHeightMetres / static_cast<double>(options.sampleHeight),
        terminalBeam.wavelengthMetres,
        options.refractiveIndex);
    const double amplitudeScale = std::sqrt(
        terminalBeam.powerWatts
        / sourceNormalizationAreaSquareMetres(source));
    const auto sourcePhase = finitePhasor(terminalBeam.phaseRadians);
    for (std::size_t y = 0; y < propagated.height(); ++y) {
        for (std::size_t x = 0; x < propagated.width(); ++x) {
            const auto envelope = sourceEnvelope(
                source,
                propagated.xCoordinateMetres(x),
                propagated.yCoordinateMetres(y));
            propagated.at(x, y) = envelope.amplitude
                * amplitudeScale * sourcePhase;
        }
    }

    return propagatePreparedField(
        bench,
        std::move(propagated),
        source.transform.translationMetres,
        source.transform.localZAxisInWorld,
        source.transform,
        target,
        pathInteractions,
        options,
        fftBackend,
        slmCommands,
        lensPrescriptions,
        amplitudeScale * amplitudeScale);
}

BeamFollowingFieldResult sampleDerivedBeamFollowingField(
    const scene::BenchScene& bench,
    const field::ComplexField2D& fieldAtSource,
    const math::RigidTransform3d& sourceFrame,
    const scene::BeamState& terminalBeam,
    std::span<const scene::BenchPathInteraction> pathInteractions,
    const BeamFollowingFieldOptions& options,
    compute::fft::IFftBackend& fftBackend,
    std::span<const PlacedSlmSparseCommand> slmCommands,
    const ray::ILensPrescriptionResolver* lensPrescriptions) {
    validateOptions(options);
    validateSparseSlmCommands(bench, slmCommands);
    scene::validateBeamState(terminalBeam);
    math::validateRigidTransform(sourceFrame);
    const auto& target = requireTarget(
        bench, terminalBeam, pathInteractions);
    validateSupportedPath(
        bench,
        sourceFrame.localZAxisInWorld,
        pathInteractions,
        lensPrescriptions);
    validateFiniteField(fieldAtSource);
    const double expectedPitchX = options.extentWidthMetres
        / static_cast<double>(options.sampleWidth);
    const double expectedPitchY = options.extentHeightMetres
        / static_cast<double>(options.sampleHeight);
    if (fieldAtSource.width() != options.sampleWidth
        || fieldAtSource.height() != options.sampleHeight
        || fieldAtSource.pitchXMetres() != expectedPitchX
        || fieldAtSource.pitchYMetres() != expectedPitchY
        || fieldAtSource.refractiveIndex() != options.refractiveIndex
        || fieldAtSource.vacuumWavelengthMetres()
            != terminalBeam.wavelengthMetres
        || !approximatelyAligned(
            sourceFrame.localZAxisInWorld,
            pathInteractions.front().incidentBeam.direction)) {
        throw std::invalid_argument(
            "derived beam-following source field, frame, and traced path do not share one sampling contract");
    }
    double peakIntensity = 0.0;
    for (const auto& sample : fieldAtSource.samples()) {
        peakIntensity = std::max(peakIntensity, std::norm(sample));
    }
    if (!std::isfinite(peakIntensity) || peakIntensity <= 0.0) {
        throw std::invalid_argument(
            "derived beam-following source field has no finite non-zero signal");
    }
    return propagatePreparedField(
        bench,
        padCentered(fieldAtSource),
        sourceFrame.translationMetres,
        sourceFrame.localZAxisInWorld,
        sourceFrame,
        target,
        pathInteractions,
        options,
        fftBackend,
        slmCommands,
        lensPrescriptions,
        peakIntensity);
}

} // namespace holobench::optics::wave
