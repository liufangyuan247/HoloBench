#include "optics/holography/BenchHologramRecording.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace holobench::optics::holography {
namespace {

field::ComplexField2D makeRelativeField(
    const field::ComplexField2D& physical,
    double referenceIrradiance) {
    if (!std::isfinite(referenceIrradiance) || referenceIrradiance <= 0.0) {
        throw std::invalid_argument(
            "thin-plate relative intensity reference must be positive and finite");
    }
    const double amplitudeScale = 1.0 / std::sqrt(referenceIrradiance);
    auto result = physical;
    for (auto& sample : result.samples()) {
        sample *= amplitudeScale;
        if (!std::isfinite(sample.real()) || !std::isfinite(sample.imag())) {
            throw std::overflow_error(
                "thin-plate relative field is not representable");
        }
    }
    return result;
}

ThinPlateRecordingResult recordThinTransmissionPlateImpl(
    const scene::BenchScene& bench,
    const PlateIncidentFieldSet& fields,
    std::uint64_t objectBranchId,
    std::uint64_t referenceBranchId,
    const ThinPlateRecordingOptions& options,
    compute::fft::IFftBackend* fftBackend) {
    if (fields.isStaleFor(bench)) {
        throw std::invalid_argument(
            "thin-plate recording requires current incident branch evidence");
    }
    const auto pair = makePlateRecordingPair(
        fields, objectBranchId, referenceBranchId);
    if (pair.geometry != PlateRecordingGeometry::Transmission) {
        throw std::invalid_argument(
            "thin-amplitude plate recording requires same-side transmission geometry");
    }
    auto sample = [&](std::uint64_t branchId) {
        return fftBackend == nullptr
            ? samplePlateIncidentField(bench, fields, branchId, options.sampling)
            : samplePlateIncidentField(
                bench, fields, branchId, options.sampling, *fftBackend);
    };
    auto object = sample(objectBranchId);
    auto reference = sample(referenceBranchId);

    ThinPlateRecordingDiagnostics diagnostics;
    diagnostics.fringeFrequencyXCyclesPerMetre
        = object.diagnostics.transverseFrequencyXCyclesPerMetre
        - reference.diagnostics.transverseFrequencyXCyclesPerMetre;
    diagnostics.fringeFrequencyYCyclesPerMetre
        = object.diagnostics.transverseFrequencyYCyclesPerMetre
        - reference.diagnostics.transverseFrequencyYCyclesPerMetre;
    const double fringeFrequency = std::hypot(
        diagnostics.fringeFrequencyXCyclesPerMetre,
        diagnostics.fringeFrequencyYCyclesPerMetre);
    diagnostics.fringePeriodMetres = fringeFrequency == 0.0
        ? std::numeric_limits<double>::infinity()
        : 1.0 / fringeFrequency;
    diagnostics.nyquistXCyclesPerMetre
        = object.diagnostics.nyquistXCyclesPerMetre;
    diagnostics.nyquistYCyclesPerMetre
        = object.diagnostics.nyquistYCyclesPerMetre;
    diagnostics.fringeCarrierSampled
        = std::abs(diagnostics.fringeFrequencyXCyclesPerMetre)
            <= diagnostics.nyquistXCyclesPerMetre
        && std::abs(diagnostics.fringeFrequencyYCyclesPerMetre)
            <= diagnostics.nyquistYCyclesPerMetre;
    diagnostics.objectPowerOnSampledWindowWatts
        = object.diagnostics.integratedPowerWatts;
    diagnostics.referencePowerOnSampledWindowWatts
        = reference.diagnostics.integratedPowerWatts;
    diagnostics.relativeIntensityReferenceWattsPerSquareMetre
        = options.relativeIntensityReferenceWattsPerSquareMetre;
    if (!diagnostics.fringeCarrierSampled) {
        throw std::invalid_argument(
            "thin-plate sampling does not resolve the object/reference fringe carrier");
    }

    auto relativeObject = makeRelativeField(
        object.field, options.relativeIntensityReferenceWattsPerSquareMetre);
    auto relativeReference = makeRelativeField(
        reference.field, options.relativeIntensityReferenceWattsPerSquareMetre);
    auto hologram = recordThinAmplitudeHologram(
        relativeObject, relativeReference, options.response);
    return {
        .plateComponentId = fields.plateComponentId,
        .sourceRevision = fields.sourceRevision,
        .pair = pair,
        .objectIncident = std::move(object),
        .referenceIncident = std::move(reference),
        .relativeObjectField = std::move(relativeObject),
        .relativeReferenceField = std::move(relativeReference),
        .hologram = std::move(hologram),
        .diagnostics = diagnostics,
    };
}

} // namespace

bool ThinPlateRecordingResult::isStaleFor(
    const scene::BenchScene& bench) const noexcept {
    return sourceRevision != bench.revision()
        || objectIncident.isStaleFor(bench)
        || referenceIncident.isStaleFor(bench);
}

ThinPlateRecordingResult recordThinTransmissionPlate(
    const scene::BenchScene& bench,
    const PlateIncidentFieldSet& fields,
    std::uint64_t objectBranchId,
    std::uint64_t referenceBranchId,
    const ThinPlateRecordingOptions& options) {
    return recordThinTransmissionPlateImpl(
        bench,
        fields,
        objectBranchId,
        referenceBranchId,
        options,
        nullptr);
}

ThinPlateRecordingResult recordThinTransmissionPlate(
    const scene::BenchScene& bench,
    const PlateIncidentFieldSet& fields,
    std::uint64_t objectBranchId,
    std::uint64_t referenceBranchId,
    const ThinPlateRecordingOptions& options,
    compute::fft::IFftBackend& fftBackend) {
    return recordThinTransmissionPlateImpl(
        bench,
        fields,
        objectBranchId,
        referenceBranchId,
        options,
        &fftBackend);
}

} // namespace holobench::optics::holography
