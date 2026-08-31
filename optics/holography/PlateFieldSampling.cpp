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

bool approximatelyParallel(math::Vec3d first, math::Vec3d second) {
    constexpr double kAlignmentTolerance = 1e-10;
    return std::abs(math::dot(math::normalized(first), math::normalized(second)))
        >= 1.0 - kAlignmentTolerance;
}

bool supportsLocalWavePath(
    const scene::BenchScene& bench,
    const scene::BenchComponent& source,
    const PlateIncidentBranch& branch,
    std::string& reason) {
    if (branch.pathInteractions.empty()) {
        reason = "ordered source-to-plate interaction evidence is unavailable";
        return false;
    }
    math::Vec3d pathDirection = source.transform.localZAxisInWorld;
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
        if (!approximatelyAligned(interaction.incidentBeam.direction, pathDirection)) {
            reason = "traced interaction directions are not connected in path order";
            return false;
        }
        if (requiresWaveRefinement(component->kind)) {
            const double incidenceCosine = std::abs(math::dot(
                math::normalized(pathDirection),
                component->transform.localZAxisInWorld));
            if (incidenceCosine <= 1e-6) {
                reason = "a local optical plane is grazing the sampled path";
                return false;
            }
            if (component->kind == scene::BenchComponentKind::IdealThinLens
                && !approximatelyParallel(
                    component->transform.localZAxisInWorld, pathDirection)) {
                reason = "an ideal powered lens is tilted relative to its incident centre ray";
                return false;
            }
        }
        if (interaction.hasOutgoingBeam) {
            const auto outgoing = math::normalized(
                interaction.outgoingBeam.direction);
            if (!approximatelyAligned(outgoing, pathDirection)
                && component->kind != scene::BenchComponentKind::PlanarMirror
                && component->kind
                    != scene::BenchComponentKind::BeamSplitterCombiner) {
                reason = "a powered or unsupported component changes the centre-ray direction";
                return false;
            }
            pathDirection = outgoing;
        }
    }
    return true;
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

bool isInsideSlmActivePixel(
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
        return false;
    }
    const double pixelX = gridX - std::floor(gridX) - 0.5;
    const double pixelY = gridY - std::floor(gridY) - 0.5;
    return std::abs(pixelX) < 0.5 * parameters.fillFactor
        && std::abs(pixelY) < 0.5 * parameters.fillFactor;
}

void applyProjectedElement(
    field::ComplexField2D& value,
    const scene::BenchComponent& component,
    const math::RigidTransform3d& fieldFrame,
    math::Vec3d propagationDirection,
    PlateFieldSamplingDiagnostics& diagnostics) {
    if (!requiresWaveRefinement(component.kind)
        || component.kind == scene::BenchComponentKind::RealLensAssembly) {
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
            case scene::BenchComponentKind::SpatialLightModulator:
                transmitted = isInsideSlmActivePixel(
                    std::get<scene::SpatialLightModulatorParameters>(
                        component.parameters),
                    local.x,
                    local.y);
                break;
            default:
                break;
            }
            if (!transmitted) {
                transformed.at(x, y) = {0.0, 0.0};
            } else if (phase != 0.0) {
                if (!std::isfinite(phase)) {
                    throw std::overflow_error(
                        "projected thin-element phase is not representable");
                }
                transformed.at(x, y) *= finitePhasor(phase);
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
        diagnostics.warnings.push_back(
            component.id
            + ": active-pixel bounds and dead space are applied with a uniform zero-phase command");
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
            "plate tangent-plane sample coordinate is not representable");
    }
    const auto x0 = static_cast<long long>(std::floor(xIndex));
    const auto y0 = static_cast<long long>(std::floor(yIndex));
    const double tx = xIndex - static_cast<double>(x0);
    const double ty = yIndex - static_cast<double>(y0);
    const auto sample = [&](long long x, long long y) {
        if (x < 0 || y < 0
            || x >= static_cast<long long>(value.width())
            || y >= static_cast<long long>(value.height())) {
            return std::complex<double> {0.0, 0.0};
        }
        return value.at(static_cast<std::size_t>(x), static_cast<std::size_t>(y));
    };
    return (1.0 - tx) * (1.0 - ty) * sample(x0, y0)
        + tx * (1.0 - ty) * sample(x0 + 1, y0)
        + (1.0 - tx) * ty * sample(x0, y0 + 1)
        + tx * ty * sample(x0 + 1, y0 + 1);
}

field::ComplexField2D sampleOnPlateTangentPlane(
    const field::ComplexField2D& envelope,
    const math::RigidTransform3d& fieldFrame,
    math::Vec3d propagationDirection,
    const scene::BenchComponent& plate,
    const PlateFieldSamplingOptions& options,
    double extentWidth,
    double extentHeight,
    PlateFieldSamplingDiagnostics& diagnostics) {
    field::ComplexField2D result(
        options.sampleWidth,
        options.sampleHeight,
        extentWidth / static_cast<double>(options.sampleWidth),
        extentHeight / static_cast<double>(options.sampleHeight),
        envelope.vacuumWavelengthMetres(),
        envelope.refractiveIndex());
    const double alignment = std::abs(math::dot(
        math::normalized(propagationDirection),
        plate.transform.localZAxisInWorld));
    diagnostics.usedPlateTangentProjection = alignment < 1.0 - 1e-10;
    for (std::size_t y = 0; y < result.height(); ++y) {
        for (std::size_t x = 0; x < result.width(); ++x) {
            const math::Vec3d localPoint {
                result.xCoordinateMetres(x) + options.centreXMetres,
                result.yCoordinateMetres(y) + options.centreYMetres,
                0.0,
            };
            const math::Vec3d worldPoint = math::transformPointLocalToWorld(
                plate.transform, localPoint);
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

SampledPlateIncidentField sampleLocalWavePath(
    const scene::BenchScene& bench,
    const PlateIncidentBranch& branch,
    const PlateFieldSamplingOptions& options,
    compute::fft::IFftBackend& fftBackend,
    SampledPlateIncidentField baseline) {
    const auto& source = requireSource(bench, branch);
    const double extentWidth = baseline.diagnostics.sampledExtentWidthMetres;
    const double extentHeight = baseline.diagnostics.sampledExtentHeightMetres;
    if (options.sampleWidth > kMaximumSamplesPerAxis / 2U
        || options.sampleHeight > kMaximumSamplesPerAxis / 2U) {
        throw std::invalid_argument(
            "beam-following local path requires a 2x padded grid no larger than 4096 samples per axis");
    }
    field::ComplexField2D propagated(
        options.sampleWidth * 2U,
        options.sampleHeight * 2U,
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
                propagated.xCoordinateMetres(x),
                propagated.yCoordinateMetres(y));
            propagated.at(x, y) = envelope.amplitude
                * amplitudeScale * sourcePhase;
        }
    }

    compute::propagation::AngularSpectrumPropagator propagator(fftBackend);
    math::Vec3d previousPoint = source.transform.translationMetres;
    math::Vec3d propagationDirection = source.transform.localZAxisInWorld;
    math::RigidTransform3d fieldFrame = source.transform;
    const scene::BenchComponent* plate = nullptr;
    for (const auto& interaction : branch.pathInteractions) {
        const double distance = math::length(
            interaction.hitPointMetres - previousPoint);
        static_cast<void>(propagator.propagateInPlace(propagated, distance));
        const auto* component = bench.find(interaction.componentId);
        if (component == nullptr) {
            throw std::logic_error(
                "local wave path component disappeared during sampling");
        }
        fieldFrame.translationMetres = interaction.hitPointMetres;
        if (component->kind != scene::BenchComponentKind::HolographicPlate) {
            applyProjectedElement(
                propagated,
                *component,
                fieldFrame,
                propagationDirection,
                baseline.diagnostics);
        } else {
            plate = component;
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
                baseline.diagnostics.usedFoldedPath = true;
                baseline.diagnostics.foldedWaveComponentIds.push_back(
                    component->id);
            }
            propagationDirection = outgoing;
        }
        previousPoint = interaction.hitPointMetres;
    }

    if (plate == nullptr) {
        throw std::logic_error(
            "local wave path did not terminate on its holographic plate");
    }
    baseline.field = sampleOnPlateTangentPlane(
        propagated,
        fieldFrame,
        propagationDirection,
        *plate,
        options,
        extentWidth,
        extentHeight,
        baseline.diagnostics);
    baseline.diagnostics.appliedLocalWavePath = true;
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
    if (baseline.diagnostics.usedTiltedElementProjection) {
        baseline.diagnostics.warnings.emplace_back(
            "tilted zero-thickness element footprints use carrier-tracked beam-normal projection; vector, polarization, thickness, and high-NA longitudinal effects are not modeled");
    }
    if (baseline.diagnostics.usedPlateTangentProjection) {
        baseline.diagnostics.warnings.emplace_back(
            "the oblique plate tangent projection restores the exact centre carrier but treats envelope evolution across the tilted window paraxially");
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
    if (!supportsLocalWavePath(bench, source, branch, reason)) {
        baseline.diagnostics.warnings.push_back(
            "local wave refinement skipped: " + reason);
        return baseline;
    }
    return sampleLocalWavePath(
        bench,
        branch,
        options,
        fftBackend,
        std::move(baseline));
}

} // namespace holobench::optics::holography
