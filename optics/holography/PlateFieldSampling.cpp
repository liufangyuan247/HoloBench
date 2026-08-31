#include "optics/holography/PlateFieldSampling.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string_view>
#include <utility>

#include "compute/fft/IFftBackend.hpp"
#include "compute/propagation/AngularSpectrumPropagator.hpp"
#include "optics/slm/SpatialLightModulator.hpp"
#include "optics/wave/FieldElements.hpp"

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
    switch (kind) {
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

bool approximatelyAligned(math::Vec3d first, math::Vec3d second) {
    constexpr double kAlignmentTolerance = 1e-10;
    return math::dot(math::normalized(first), math::normalized(second))
        >= 1.0 - kAlignmentTolerance;
}

bool supportsCoaxialWavePath(
    const scene::BenchScene& bench,
    const scene::BenchComponent& source,
    const PlateIncidentBranch& branch,
    std::string& reason) {
    if (branch.pathInteractions.empty()) {
        reason = "ordered source-to-plate interaction evidence is unavailable";
        return false;
    }
    const math::Vec3d pathDirection = source.transform.localZAxisInWorld;
    for (const auto& interaction : branch.pathInteractions) {
        const auto* component = bench.find(interaction.componentId);
        if (component == nullptr) {
            reason = "a traced component no longer exists";
            return false;
        }
        if (component->kind == scene::BenchComponentKind::RealLensAssembly) {
            reason = "real-lens prescription wave propagation is not available";
            return false;
        }
        if (!approximatelyAligned(
                interaction.incidentBeam.direction, pathDirection)) {
            reason = "folded or direction-changing paths require plane resampling";
            return false;
        }
        if (interaction.hasOutgoingBeam
            && !approximatelyAligned(
                interaction.outgoingBeam.direction, pathDirection)) {
            reason = "a component changes the centre-ray direction";
            return false;
        }
        if (!approximatelyAligned(
                component->transform.localZAxisInWorld, pathDirection)
            || !approximatelyAligned(
                component->transform.localXAxisInWorld,
                source.transform.localXAxisInWorld)
            || !approximatelyAligned(
                component->transform.localYAxisInWorld,
                source.transform.localYAxisInWorld)) {
            reason = "a local optical plane is tilted or rotated relative to the sampled path";
            return false;
        }
    }
    return true;
}

void applyCoaxialElement(
    field::ComplexField2D& value,
    const scene::BenchComponent& component,
    math::Vec3d hitPointMetres,
    const PlateFieldSamplingOptions& options,
    PlateFieldSamplingDiagnostics& diagnostics) {
    const auto localHit = math::transformPointWorldToLocal(
        component.transform, hitPointMetres);
    const double centreX = -localHit.x - options.centreXMetres;
    const double centreY = -localHit.y - options.centreYMetres;
    switch (component.kind) {
    case scene::BenchComponentKind::IdealThinLens: {
        const auto& parameters = std::get<scene::IdealThinLensParameters>(
            component.parameters);
        static_cast<void>(wave::applyCircularAperture(value, {
            .radiusMetres = 0.5 * parameters.clearApertureDiameterMetres,
            .centerXMetres = centreX,
            .centerYMetres = centreY,
        }));
        static_cast<void>(wave::applyIdealThinLensPhase(value, {
            .focalLengthMetres = parameters.focalLengthMetres,
            .centerXMetres = centreX,
            .centerYMetres = centreY,
        }));
        break;
    }
    case scene::BenchComponentKind::Aperture: {
        const auto& parameters = std::get<scene::ApertureParameters>(
            component.parameters);
        if (parameters.shape == scene::ApertureShape::Circular) {
            static_cast<void>(wave::applyEllipticalAperture(value, {
                .halfWidthMetres = 0.5 * parameters.widthMetres,
                .halfHeightMetres = 0.5 * parameters.heightMetres,
                .centerXMetres = centreX,
                .centerYMetres = centreY,
            }));
        } else {
            static_cast<void>(wave::applyRectangularAperture(value, {
                .halfWidthMetres = 0.5 * parameters.widthMetres,
                .halfHeightMetres = 0.5 * parameters.heightMetres,
                .centerXMetres = centreX,
                .centerYMetres = centreY,
            }));
        }
        break;
    }
    case scene::BenchComponentKind::SpatialFilter: {
        const auto& parameters = std::get<scene::SpatialFilterParameters>(
            component.parameters);
        static_cast<void>(wave::applyCircularAperture(value, {
            .radiusMetres = 0.5 * parameters.pinholeDiameterMetres,
            .centerXMetres = centreX,
            .centerYMetres = centreY,
        }));
        diagnostics.warnings.push_back(
            component.id
            + ": modeled as its explicit pinhole plane; the compound focusing objective requires separately placed lenses");
        break;
    }
    case scene::BenchComponentKind::SpatialLightModulator: {
        const auto& parameters
            = std::get<scene::SpatialLightModulatorParameters>(
                component.parameters);
        slm::PixelatedSlmParameters slmParameters;
        slmParameters.pixelColumns = parameters.pixelWidth;
        slmParameters.pixelRows = parameters.pixelHeight;
        slmParameters.pixelPitchXMetres = parameters.widthMetres
            / static_cast<double>(parameters.pixelWidth);
        slmParameters.pixelPitchYMetres = parameters.heightMetres
            / static_cast<double>(parameters.pixelHeight);
        slmParameters.fillFactorX = parameters.fillFactor;
        slmParameters.fillFactorY = parameters.fillFactor;
        slmParameters.centerXMetres = centreX;
        slmParameters.centerYMetres = centreY;
        slmParameters.mode = slm::ModulationMode::Phase;
        static_cast<void>(slm::applyUniformPixelatedSlm(
            value, slmParameters, 0.0));
        diagnostics.warnings.push_back(
            component.id
            + ": active-pixel bounds and dead space are applied with a uniform zero-phase command");
        break;
    }
    default:
        return;
    }
    diagnostics.appliedWaveComponentIds.push_back(component.id);
}

SampledPlateIncidentField sampleCoaxialWavePath(
    const scene::BenchScene& bench,
    const PlateIncidentBranch& branch,
    const PlateFieldSamplingOptions& options,
    compute::fft::IFftBackend& fftBackend,
    SampledPlateIncidentField baseline) {
    const auto& source = requireSource(bench, branch);
    const double extentWidth = baseline.diagnostics.sampledExtentWidthMetres;
    const double extentHeight = baseline.diagnostics.sampledExtentHeightMetres;
    field::ComplexField2D propagated(
        options.sampleWidth,
        options.sampleHeight,
        extentWidth / static_cast<double>(options.sampleWidth),
        extentHeight / static_cast<double>(options.sampleHeight),
        branch.beam.wavelengthMetres,
        options.refractiveIndex);

    const double amplitudeScale = std::sqrt(
        branch.beam.powerWatts / sourceNormalizationAreaSquareMetres(source));
    const auto sourcePhase = finitePhasor(branch.beam.phaseRadians);
    for (std::size_t y = 0; y < propagated.height(); ++y) {
        for (std::size_t x = 0; x < propagated.width(); ++x) {
            const auto envelope = sourceEnvelope(
                source,
                propagated.xCoordinateMetres(x) + options.centreXMetres,
                propagated.yCoordinateMetres(y) + options.centreYMetres);
            propagated.at(x, y) = envelope.amplitude
                * amplitudeScale * sourcePhase;
        }
    }

    compute::propagation::AngularSpectrumPropagator propagator(fftBackend);
    math::Vec3d previousPoint = source.transform.translationMetres;
    for (const auto& interaction : branch.pathInteractions) {
        const double distance = math::length(
            interaction.hitPointMetres - previousPoint);
        static_cast<void>(propagator.propagateInPlace(propagated, distance));
        const auto* component = bench.find(interaction.componentId);
        if (component == nullptr) {
            throw std::logic_error(
                "coaxial path component disappeared during sampling");
        }
        if (component->kind != scene::BenchComponentKind::HolographicPlate) {
            applyCoaxialElement(
                propagated,
                *component,
                interaction.hitPointMetres,
                options,
                baseline.diagnostics);
        }
        previousPoint = interaction.hitPointMetres;
    }

    baseline.field = std::move(propagated);
    baseline.diagnostics.appliedCoaxialWavePath = true;
    baseline.diagnostics.usesApproximateSourceEnvelope = false;
    baseline.diagnostics.illuminatedSampleCount = 0U;
    baseline.diagnostics.integratedPowerWatts = 0.0;
    baseline.diagnostics.supportTouchesPlateBoundary = false;
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
            if (isBoundarySample(
                    x, y, baseline.field.width(), baseline.field.height())
                && intensity > kBoundaryEnvelopeThreshold
                    * kBoundaryEnvelopeThreshold * amplitudeScale
                    * amplitudeScale) {
                baseline.diagnostics.supportTouchesPlateBoundary = true;
            }
        }
    }
    if (baseline.diagnostics.supportTouchesPlateBoundary) {
        baseline.diagnostics.warnings.emplace_back(
            "propagated field reaches the sampled boundary; periodic FFT wrap may contaminate this window");
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
    compute::fft::IFftBackend& fftBackend) {
    auto baseline = samplePlateIncidentField(
        bench, fields, branchId, options);
    const auto& branch = requireBranch(fields, branchId);
    if (!hasWaveRefinementComponent(bench, branch)) {
        return baseline;
    }
    const auto& source = requireSource(bench, branch);
    std::string reason;
    if (!supportsCoaxialWavePath(bench, source, branch, reason)) {
        baseline.diagnostics.warnings.push_back(
            "coaxial wave refinement skipped: " + reason);
        return baseline;
    }
    return sampleCoaxialWavePath(
        bench,
        branch,
        options,
        fftBackend,
        std::move(baseline));
}

} // namespace holobench::optics::holography
