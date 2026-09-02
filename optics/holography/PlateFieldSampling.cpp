#include "optics/holography/PlateFieldSampling.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <iterator>
#include <limits>
#include <numbers>
#include <set>
#include <stdexcept>
#include <utility>

#include "compute/fft/IFftBackend.hpp"

namespace holobench::optics::holography {
namespace {

constexpr std::size_t kMaximumSamplesPerAxis = 4096;
constexpr double kBoundaryEnvelopeThreshold = 1e-6;

const PlateIncidentBranch& requireBranch(
    const PlateIncidentFieldSet& fields,
    std::uint64_t branchId) {
    const auto found = std::find_if(
        fields.branches.begin(), fields.branches.end(),
        [branchId](const auto& branch) {
            return branch.beam.provenance.branchId == branchId;
        });
    if (found == fields.branches.end()) {
        throw std::invalid_argument(
            "sampled plate branch was not found in the incident field set");
    }
    return *found;
}

const scene::BenchComponent& requireSource(
    const scene::BenchScene& bench,
    const PlateIncidentBranch& branch) {
    if (branch.beam.provenance.componentPath.empty()) {
        throw std::invalid_argument("sampled plate branch has no source provenance");
    }
    const auto* source = bench.find(branch.beam.provenance.componentPath.front());
    if (source == nullptr
        || (source->kind != scene::BenchComponentKind::LaserSource
            && source->kind != scene::BenchComponentKind::ObjectWavefrontSource)) {
        throw std::invalid_argument(
            "sampled plate branch does not begin at a supported source");
    }
    return *source;
}

void validateOptions(const PlateFieldSamplingOptions& options) {
    if (options.sampleWidth < 2U || options.sampleHeight < 2U
        || options.sampleWidth > kMaximumSamplesPerAxis
        || options.sampleHeight > kMaximumSamplesPerAxis) {
        throw std::invalid_argument(
            "plate field sample dimensions must each be in [2, 4096]");
    }
    if (!std::isfinite(options.refractiveIndex) || options.refractiveIndex <= 0.0) {
        throw std::invalid_argument(
            "plate field refractive index must be positive and finite");
    }
    if (!std::isfinite(options.extentWidthMetres)
        || !std::isfinite(options.extentHeightMetres)
        || options.extentWidthMetres < 0.0
        || options.extentHeightMetres < 0.0
        || !std::isfinite(options.centreXMetres)
        || !std::isfinite(options.centreYMetres)) {
        throw std::invalid_argument(
            "plate field analysis window must be finite with non-negative extents");
    }
}

bool isBoundarySample(
    std::size_t x,
    std::size_t y,
    std::size_t width,
    std::size_t height) noexcept {
    return x == 0U || y == 0U || x + 1U == width || y + 1U == height;
}

struct EnvelopeSample final {
    double amplitude = 0.0;
    bool illuminated = false;
};

EnvelopeSample sourceEnvelope(
    const scene::BenchComponent& source,
    double beamXMetres,
    double beamYMetres) {
    if (source.kind == scene::BenchComponentKind::LaserSource) {
        const auto& parameters = std::get<scene::LaserSourceParameters>(
            source.parameters);
        const double radius = std::hypot(beamXMetres, beamYMetres);
        if (parameters.profile == scene::LaserBeamProfile::Collimated) {
            const bool inside = radius <= parameters.beamRadiusMetres;
            return {.amplitude = inside ? 1.0 : 0.0, .illuminated = inside};
        }
        const double normalizedRadius = radius / parameters.beamRadiusMetres;
        const double amplitude = std::exp(-normalizedRadius * normalizedRadius);
        return {
            .amplitude = amplitude,
            .illuminated = amplitude > 0.0,
        };
    }

    const auto& parameters = std::get<scene::ObjectWavefrontSourceParameters>(
        source.parameters);
    const bool inside = std::abs(beamXMetres) <= 0.5 * parameters.widthMetres
        && std::abs(beamYMetres) <= 0.5 * parameters.heightMetres;
    return {.amplitude = inside ? 1.0 : 0.0, .illuminated = inside};
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
    const auto& parameters = std::get<scene::ObjectWavefrontSourceParameters>(
        source.parameters);
    return parameters.widthMetres * parameters.heightMetres;
}

bool requiresWaveRefinement(scene::BenchComponentKind kind) noexcept {
    return wave::requiresBeamFollowingWaveTransform(kind);
}

bool hasWaveRefinementComponent(
    const scene::BenchScene& bench,
    const PlateIncidentBranch& branch) {
    return std::any_of(
        branch.beam.provenance.componentPath.begin(),
        branch.beam.provenance.componentPath.end(),
        [&bench](const auto& componentId) {
            const auto* component = bench.find(componentId);
            return component != nullptr
                && requiresWaveRefinement(component->kind);
        });
}

void appendRefinementWarnings(
    const scene::BenchScene& bench,
    const PlateIncidentBranch& branch,
    PlateFieldSamplingDiagnostics& diagnostics) {
    for (const auto& componentId : branch.beam.provenance.componentPath) {
        const auto* component = bench.find(componentId);
        if (component != nullptr && requiresWaveRefinement(component->kind)) {
            diagnostics.warnings.push_back(
                componentId
                + ": centreline routing is present but its sampled wave transform is not yet applied");
        }
    }
}

std::complex<double> finitePhasor(double phaseRadians) {
    if (!std::isfinite(phaseRadians)) {
        throw std::overflow_error("sampled plate phase is not representable");
    }
    return std::polar(
        1.0,
        std::remainder(phaseRadians, 2.0 * std::numbers::pi));
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

SampledPlateIncidentField sampleLocalWavePath(
    const scene::BenchScene& bench,
    const PlateIncidentBranch& branch,
    const PlateFieldSamplingOptions& options,
    compute::fft::IFftBackend& fftBackend,
    SampledPlateIncidentField baseline,
    std::span<const PlacedSlmSparseCommand> slmCommands,
    const ray::ILensPrescriptionResolver* lensPrescriptions,
    const slm::ISlmResponseResolver* slmResponses,
    double environmentTemperatureKelvin) {
    const double extentWidth = baseline.diagnostics.sampledExtentWidthMetres;
    const double extentHeight = baseline.diagnostics.sampledExtentHeightMetres;
    auto sampled = wave::sampleBeamFollowingField(
        bench,
        branch.beam,
        branch.pathInteractions,
        {
            .sampleWidth = options.sampleWidth,
            .sampleHeight = options.sampleHeight,
            .extentWidthMetres = extentWidth,
            .extentHeightMetres = extentHeight,
            .centreXMetres = options.centreXMetres,
            .centreYMetres = options.centreYMetres,
            .refractiveIndex = options.refractiveIndex,
            .slmResponses = slmResponses,
            .environmentTemperatureKelvin
                = environmentTemperatureKelvin,
        },
        fftBackend,
        slmCommands,
        lensPrescriptions);
    baseline.field = std::move(sampled.fieldAtTarget);
    baseline.diagnostics.usedTiltedElementProjection
        = sampled.diagnostics.usedTiltedElementProjection;
    baseline.diagnostics.usedFoldedPath
        = sampled.diagnostics.usedFoldedPath;
    baseline.diagnostics.usedPlateTangentProjection
        = sampled.diagnostics.usedTargetTangentProjection;
    baseline.diagnostics.supportTouchesPlateBoundary
        = sampled.diagnostics.supportTouchesBoundary;
    baseline.diagnostics.appliedWaveComponentIds
        = std::move(sampled.diagnostics.appliedWaveComponentIds);
    baseline.diagnostics.foldedWaveComponentIds
        = std::move(sampled.diagnostics.foldedWaveComponentIds);
    baseline.diagnostics.appliedSlmCommandIds
        = std::move(sampled.diagnostics.appliedSlmCommandIds);
    baseline.diagnostics.appliedSlmCalibrationIds
        = std::move(sampled.diagnostics.appliedSlmCalibrationIds);
    baseline.diagnostics.appliedRealLensPrescriptionIds
        = std::move(
            sampled.diagnostics.appliedRealLensPrescriptionIds);
    baseline.diagnostics.warnings.insert(
        baseline.diagnostics.warnings.end(),
        std::make_move_iterator(sampled.diagnostics.warnings.begin()),
        std::make_move_iterator(sampled.diagnostics.warnings.end()));
    baseline.diagnostics.appliedLocalWavePath = true;
    baseline.diagnostics.usesApproximateSourceEnvelope = false;
    baseline.diagnostics.illuminatedSampleCount = 0U;
    baseline.diagnostics.integratedPowerWatts = 0.0;
    baseline.diagnostics.warnings.erase(
        std::remove_if(
            baseline.diagnostics.warnings.begin(),
            baseline.diagnostics.warnings.end(),
            [](const std::string& warning) {
                return warning.find(
                    "sampled wave transform is not yet applied")
                    != std::string::npos
                    || warning.find(
                        "propagated waist curvature awaits local-plane refinement")
                    != std::string::npos
                    || warning.find(
                        "incident source support reaches the sampled plate boundary")
                    != std::string::npos;
            }),
        baseline.diagnostics.warnings.end());

    const double pixelArea = baseline.field.pitchXMetres()
        * baseline.field.pitchYMetres();
    const double incidenceCosine = std::abs(branch.localDirection.z);
    for (std::size_t y = 0; y < baseline.field.height(); ++y) {
        for (std::size_t x = 0; x < baseline.field.width(); ++x) {
            const double intensity = std::norm(baseline.field.at(x, y));
            baseline.diagnostics.integratedPowerWatts += intensity
                * pixelArea * incidenceCosine;
            if (intensity > 0.0) {
                ++baseline.diagnostics.illuminatedSampleCount;
            }
        }
    }
    return baseline;
}

} // namespace

bool SampledPlateIncidentField::isStaleFor(
    const scene::BenchScene& bench) const noexcept {
    const auto* plate = bench.find(plateComponentId);
    return sourceRevision != bench.revision()
        || plate == nullptr
        || plate->kind != scene::BenchComponentKind::HolographicPlate;
}

SampledPlateIncidentField samplePlateIncidentField(
    const scene::BenchScene& bench,
    const PlateIncidentFieldSet& fields,
    std::uint64_t branchId,
    const PlateFieldSamplingOptions& options) {
    validateOptions(options);
    if (fields.isStaleFor(bench)) {
        throw std::invalid_argument(
            "sampled plate field requires current incident branch evidence");
    }
    const auto* plate = bench.find(fields.plateComponentId);
    if (plate == nullptr) {
        throw std::invalid_argument("sampled plate component was not found");
    }
    const auto& plateParameters
        = std::get<scene::HolographicPlateParameters>(plate->parameters);
    const auto& branch = requireBranch(fields, branchId);
    scene::validateBeamState(branch.beam);
    const auto& source = requireSource(bench, branch);

    const double extentWidth = options.extentWidthMetres == 0.0
        ? plateParameters.widthMetres
        : options.extentWidthMetres;
    const double extentHeight = options.extentHeightMetres == 0.0
        ? plateParameters.heightMetres
        : options.extentHeightMetres;
    if (extentWidth > plateParameters.widthMetres
        || extentHeight > plateParameters.heightMetres
        || std::abs(options.centreXMetres) + 0.5 * extentWidth
            > 0.5 * plateParameters.widthMetres
        || std::abs(options.centreYMetres) + 0.5 * extentHeight
            > 0.5 * plateParameters.heightMetres) {
        throw std::invalid_argument(
            "plate field analysis window must fit inside the physical plate");
    }
    const double pitchX = extentWidth
        / static_cast<double>(options.sampleWidth);
    const double pitchY = extentHeight
        / static_cast<double>(options.sampleHeight);
    field::ComplexField2D sampled(
        options.sampleWidth,
        options.sampleHeight,
        pitchX,
        pitchY,
        branch.beam.wavelengthMetres,
        options.refractiveIndex);

    PlateFieldSamplingDiagnostics diagnostics;
    diagnostics.sampledExtentWidthMetres = extentWidth;
    diagnostics.sampledExtentHeightMetres = extentHeight;
    diagnostics.sampledCentreXMetres = options.centreXMetres;
    diagnostics.sampledCentreYMetres = options.centreYMetres;
    diagnostics.usesLocalAnalysisWindow
        = extentWidth != plateParameters.widthMetres
        || extentHeight != plateParameters.heightMetres
        || options.centreXMetres != 0.0
        || options.centreYMetres != 0.0;
    diagnostics.transverseFrequencyXCyclesPerMetre
        = options.refractiveIndex * branch.localDirection.x
        / branch.beam.wavelengthMetres;
    diagnostics.transverseFrequencyYCyclesPerMetre
        = options.refractiveIndex * branch.localDirection.y
        / branch.beam.wavelengthMetres;
    diagnostics.nyquistXCyclesPerMetre = 0.5 / pitchX;
    diagnostics.nyquistYCyclesPerMetre = 0.5 / pitchY;
    diagnostics.carrierSampled
        = std::abs(diagnostics.transverseFrequencyXCyclesPerMetre)
            <= diagnostics.nyquistXCyclesPerMetre
        && std::abs(diagnostics.transverseFrequencyYCyclesPerMetre)
            <= diagnostics.nyquistYCyclesPerMetre;
    diagnostics.usesApproximateSourceEnvelope
        = source.kind == scene::BenchComponentKind::LaserSource
        && std::get<scene::LaserSourceParameters>(source.parameters).profile
            == scene::LaserBeamProfile::Gaussian;

    double discreteFluxWeight = 0.0;
    const double pixelArea = pitchX * pitchY;
    const double incidenceCosine = std::abs(branch.localDirection.z);
    const double vacuumWavenumber = 2.0 * std::numbers::pi
        / branch.beam.wavelengthMetres;
    const double mediumWavenumber = vacuumWavenumber * options.refractiveIndex;
    const double phaseAtHit = std::fma(
        vacuumWavenumber,
        branch.beam.accumulatedOpticalPathMetres,
        branch.beam.phaseRadians);

    const double normalizationArea = sourceNormalizationAreaSquareMetres(source);
    const double amplitudeScale = std::sqrt(
        branch.beam.powerWatts / normalizationArea);
    if (!std::isfinite(amplitudeScale)) {
        throw std::overflow_error(
            "sampled plate source-power normalization is not representable");
    }

    for (std::size_t y = 0; y < sampled.height(); ++y) {
        for (std::size_t x = 0; x < sampled.width(); ++x) {
            const math::Vec3d localPoint {
                sampled.xCoordinateMetres(x) + options.centreXMetres,
                sampled.yCoordinateMetres(y) + options.centreYMetres,
                0.0,
            };
            const auto worldPoint = math::transformPointLocalToWorld(
                plate->transform, localPoint);
            const auto beamPoint = math::transformPointWorldToLocal(
                branch.beam.localFrame, worldPoint);
            const auto envelope = sourceEnvelope(source, beamPoint.x, beamPoint.y);
            if (!envelope.illuminated) {
                sampled.at(x, y) = {0.0, 0.0};
                continue;
            }
            const math::Vec3d relativeLocal = localPoint - branch.localHitPointMetres;
            const double transverseDistance = std::fma(
                branch.localDirection.x,
                relativeLocal.x,
                branch.localDirection.y * relativeLocal.y);
            const double phase = std::fma(
                mediumWavenumber, transverseDistance, phaseAtHit);
            sampled.at(x, y) = amplitudeScale
                * envelope.amplitude * finitePhasor(phase);
            discreteFluxWeight += envelope.amplitude * envelope.amplitude
                * pixelArea * incidenceCosine;
            ++diagnostics.illuminatedSampleCount;
            if (isBoundarySample(
                    x, y, sampled.width(), sampled.height())
                && envelope.amplitude > kBoundaryEnvelopeThreshold) {
                diagnostics.supportTouchesPlateBoundary = true;
            }
        }
    }

    if (!std::isfinite(discreteFluxWeight) || discreteFluxWeight <= 0.0) {
        throw std::invalid_argument(
            "incident branch has no sampled support on the holographic plate");
    }
    for (const auto& value : sampled.samples()) {
        if (!std::isfinite(value.real()) || !std::isfinite(value.imag())) {
            throw std::overflow_error(
                "sampled plate field contains a non-finite value");
        }
    }
    diagnostics.integratedPowerWatts = discreteFluxWeight
        * amplitudeScale * amplitudeScale;
    if (!diagnostics.carrierSampled) {
        diagnostics.warnings.emplace_back(
            "plate sampling does not resolve this branch's transverse carrier");
    }
    if (diagnostics.supportTouchesPlateBoundary) {
        diagnostics.warnings.emplace_back(
            "incident source support reaches the sampled plate boundary");
    }
    if (diagnostics.usesApproximateSourceEnvelope) {
        diagnostics.warnings.emplace_back(
            "Gaussian sampling uses the source radius at the observer; propagated waist curvature awaits local-plane refinement");
    }
    appendRefinementWarnings(bench, branch, diagnostics);

    return {
        .plateComponentId = fields.plateComponentId,
        .sourceRevision = fields.sourceRevision,
        .branchId = branchId,
        .role = branch.role,
        .side = branch.side,
        .field = std::move(sampled),
        .diagnostics = std::move(diagnostics),
    };
}

SampledPlateIncidentField samplePlateIncidentField(
    const scene::BenchScene& bench,
    const PlateIncidentFieldSet& fields,
    std::uint64_t branchId,
    const PlateFieldSamplingOptions& options,
    compute::fft::IFftBackend& fftBackend,
    std::span<const PlacedSlmSparseCommand> slmCommands,
    const ray::ILensPrescriptionResolver* lensPrescriptions,
    const slm::ISlmResponseResolver* slmResponses,
    double environmentTemperatureKelvin) {
    validateSparseSlmCommands(bench, slmCommands);
    auto baseline = samplePlateIncidentField(
        bench, fields, branchId, options);
    const auto& branch = requireBranch(fields, branchId);
    if (!hasWaveRefinementComponent(bench, branch)) {
        return baseline;
    }
    wave::validateBeamFollowingFieldPath(
        bench,
        branch.beam,
        branch.pathInteractions,
        lensPrescriptions);
    return sampleLocalWavePath(
        bench,
        branch,
        options,
        fftBackend,
        std::move(baseline),
        slmCommands,
        lensPrescriptions,
        slmResponses,
        environmentTemperatureKelvin);
}

} // namespace holobench::optics::holography
