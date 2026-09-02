#include "optics/holography/BenchRgbHologram.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <utility>
#include <vector>

#include "compute/fft/IFftBackend.hpp"

namespace holobench::optics::holography {
namespace {

struct Candidate final {
    PlateBranchPairSelection selection;
    double wavelengthMetres = 0.0;
};

RgbThinPlateRecordingResult recordRgbThinTransmissionPlateImpl(
    const scene::BenchScene& bench,
    const PlateIncidentFieldSet& fields,
    const std::array<PlateBranchPairSelection, 3>& selections,
    const ThinPlateRecordingOptions& options,
    compute::fft::IFftBackend* fftBackend,
    const ray::ILensPrescriptionResolver* lensPrescriptions,
    const slm::ISlmResponseResolver* slmResponses,
    double environmentTemperatureKelvin);

} // namespace

bool RgbThinPlateRecordingResult::isStaleFor(
    const scene::BenchScene& bench) const noexcept {
    return sourceRevision != bench.revision()
        || std::any_of(
            channels.begin(), channels.end(),
            [&bench](const auto& channel) {
                return channel.isStaleFor(bench);
            });
}

bool RgbThinPlateReplayResult::isStaleFor(
    const scene::BenchScene& bench) const noexcept {
    return sourceRevision != bench.revision()
        || std::any_of(
            channels.begin(), channels.end(),
            [&bench](const auto& channel) {
                return channel.isStaleFor(bench);
            });
}

bool RgbVolumePlateRecordingResult::isStaleFor(
    const scene::BenchScene& bench) const noexcept {
    return sourceRevision != bench.revision()
        || std::any_of(
            channels.begin(), channels.end(),
            [&bench](const auto& channel) {
                return channel.isStaleFor(bench);
            });
}

bool RgbVolumePlateReplayResult::isStaleFor(
    const scene::BenchScene& bench) const noexcept {
    return sourceRevision != bench.revision()
        || std::any_of(
            channels.begin(), channels.end(),
            [&bench](const auto& channel) {
                return channel.isStaleFor(bench);
            });
}

namespace {

std::array<PlateBranchPairSelection, 3> selectRgbPairs(
    const PlateIncidentFieldSet& fields,
    PlateRecordingGeometry requiredGeometry,
    const char* geometryLabel) {
    std::vector<Candidate> candidates;
    for (const auto& object : fields.branches) {
        if (object.role != RecordingBranchRole::Object) {
            continue;
        }
        for (const auto& reference : fields.branches) {
            if (reference.role != RecordingBranchRole::Reference
                || !scene::canInterfere(object.beam, reference.beam)) {
                continue;
            }
            const auto pair = makePlateRecordingPair(
                fields,
                object.beam.provenance.branchId,
                reference.beam.provenance.branchId);
            if (pair.geometry != requiredGeometry) {
                throw std::invalid_argument(
                    std::string("RGB ") + geometryLabel
                    + " recording has a compatible pair with the wrong geometry");
            }
            candidates.push_back({
                .selection = {
                    .objectBranchId = pair.objectBranchId,
                    .referenceBranchId = pair.referenceBranchId,
                },
                .wavelengthMetres = pair.wavelengthMetres,
            });
        }
    }
    if (candidates.size() != 3U) {
        throw std::invalid_argument(
            std::string("RGB ") + geometryLabel
            + " recording requires exactly three unambiguous compatible pairs");
    }
    std::sort(
        candidates.begin(), candidates.end(),
        [](const Candidate& lhs, const Candidate& rhs) {
            return lhs.wavelengthMetres > rhs.wavelengthMetres;
        });
    if (!(candidates[0].wavelengthMetres > candidates[1].wavelengthMetres
            && candidates[1].wavelengthMetres
                > candidates[2].wavelengthMetres)) {
        throw std::invalid_argument(
            "RGB recording requires three distinct ordered wavelengths");
    }
    return {{
        candidates[0].selection,
        candidates[1].selection,
        candidates[2].selection,
    }};
}

} // namespace

std::array<PlateBranchPairSelection, 3> selectRgbThinTransmissionPairs(
    const PlateIncidentFieldSet& fields) {
    return selectRgbPairs(
        fields, PlateRecordingGeometry::Transmission, "thin transmission");
}

std::array<PlateBranchPairSelection, 3> selectRgbReflectionPairs(
    const PlateIncidentFieldSet& fields) {
    return selectRgbPairs(
        fields, PlateRecordingGeometry::Reflection, "reflection/Denisyuk");
}

RgbThinPlateRecordingResult recordRgbThinTransmissionPlate(
    const scene::BenchScene& bench,
    const PlateIncidentFieldSet& fields,
    const std::array<PlateBranchPairSelection, 3>& selections,
    const ThinPlateRecordingOptions& options) {
    return recordRgbThinTransmissionPlateImpl(
        bench,
        fields,
        selections,
        options,
        nullptr,
        nullptr,
        nullptr,
        293.15);
}

RgbThinPlateRecordingResult recordRgbThinTransmissionPlate(
    const scene::BenchScene& bench,
    const PlateIncidentFieldSet& fields,
    const std::array<PlateBranchPairSelection, 3>& selections,
    const ThinPlateRecordingOptions& options,
    compute::fft::IFftBackend& fftBackend,
    const ray::ILensPrescriptionResolver* lensPrescriptions,
    const slm::ISlmResponseResolver* slmResponses,
    double environmentTemperatureKelvin) {
    return recordRgbThinTransmissionPlateImpl(
        bench,
        fields,
        selections,
        options,
        &fftBackend,
        lensPrescriptions,
        slmResponses,
        environmentTemperatureKelvin);
}

namespace {

RgbThinPlateRecordingResult recordRgbThinTransmissionPlateImpl(
    const scene::BenchScene& bench,
    const PlateIncidentFieldSet& fields,
    const std::array<PlateBranchPairSelection, 3>& selections,
    const ThinPlateRecordingOptions& options,
    compute::fft::IFftBackend* fftBackend,
    const ray::ILensPrescriptionResolver* lensPrescriptions,
    const slm::ISlmResponseResolver* slmResponses,
    double environmentTemperatureKelvin) {
    if (fields.isStaleFor(bench)) {
        throw std::invalid_argument(
            "RGB thin recording requires current plate incident evidence");
    }
    std::array<PlateRecordingPair, 3> pairs {
        makePlateRecordingPair(
            fields,
            selections[0].objectBranchId,
            selections[0].referenceBranchId),
        makePlateRecordingPair(
            fields,
            selections[1].objectBranchId,
            selections[1].referenceBranchId),
        makePlateRecordingPair(
            fields,
            selections[2].objectBranchId,
            selections[2].referenceBranchId),
    };
    if (!(pairs[0].wavelengthMetres > pairs[1].wavelengthMetres
            && pairs[1].wavelengthMetres > pairs[2].wavelengthMetres)) {
        throw std::invalid_argument(
            "RGB recording selections must be ordered red, green, blue by descending wavelength");
    }
    for (const auto& pair : pairs) {
        if (pair.geometry != PlateRecordingGeometry::Transmission) {
            throw std::invalid_argument(
                "RGB thin recording requires transmission geometry");
        }
    }
    auto record = [&](std::size_t index) {
        return fftBackend == nullptr
            ? recordThinTransmissionPlate(
                bench,
                fields,
                selections[index].objectBranchId,
                selections[index].referenceBranchId,
                options)
            : recordThinTransmissionPlate(
                bench,
                fields,
                selections[index].objectBranchId,
                selections[index].referenceBranchId,
                options,
                *fftBackend,
                lensPrescriptions,
                slmResponses,
                environmentTemperatureKelvin);
    };
    return {
        .plateComponentId = fields.plateComponentId,
        .sourceRevision = fields.sourceRevision,
        .channels = {{
            record(0U),
            record(1U),
            record(2U),
        }},
    };
}

} // namespace

RgbThinPlateReplayResult replayRgbThinTransmissionToObservation(
    const scene::BenchScene& bench,
    const RgbThinPlateRecordingResult& recording,
    std::string observationComponentId,
    ThinPlateReplayKind replayKind,
    compute::fft::IFftBackend& fftBackend) {
    if (recording.isStaleFor(bench)) {
        throw std::invalid_argument(
            "RGB thin replay requires a current three-channel recording");
    }
    const std::string sharedObservationId = observationComponentId;
    return {
        .plateComponentId = recording.plateComponentId,
        .observationComponentId = std::move(observationComponentId),
        .sourceRevision = recording.sourceRevision,
        .channels = {{
            replayThinTransmissionToObservation(
                bench,
                recording.channels[0],
                sharedObservationId,
                replayKind,
                fftBackend),
            replayThinTransmissionToObservation(
                bench,
                recording.channels[1],
                sharedObservationId,
                replayKind,
                fftBackend),
            replayThinTransmissionToObservation(
                bench,
                recording.channels[2],
                sharedObservationId,
                replayKind,
                fftBackend),
        }},
    };
}

RgbVolumePlateRecordingResult recordRgbReflectionVolumePlate(
    const scene::BenchScene& bench,
    const PlateIncidentFieldSet& fields,
    const std::array<PlateBranchPairSelection, 3>& selections,
    const VolumePlateMaterial& material) {
    if (fields.isStaleFor(bench)) {
        throw std::invalid_argument(
            "RGB reflection recording requires current plate incident evidence");
    }
    const std::array<PlateRecordingPair, 3> pairs {{
        makePlateRecordingPair(fields, selections[0].objectBranchId,
            selections[0].referenceBranchId),
        makePlateRecordingPair(fields, selections[1].objectBranchId,
            selections[1].referenceBranchId),
        makePlateRecordingPair(fields, selections[2].objectBranchId,
            selections[2].referenceBranchId),
    }};
    if (!(pairs[0].wavelengthMetres > pairs[1].wavelengthMetres
            && pairs[1].wavelengthMetres > pairs[2].wavelengthMetres)) {
        throw std::invalid_argument(
            "RGB reflection selections must be ordered red, green, blue");
    }
    for (const auto& pair : pairs) {
        if (pair.geometry != PlateRecordingGeometry::Reflection) {
            throw std::invalid_argument(
                "RGB volume recording requires reflection geometry");
        }
    }
    return {
        .plateComponentId = fields.plateComponentId,
        .sourceRevision = fields.sourceRevision,
        .channels = {{
            recordVolumePlate(bench, fields, selections[0].objectBranchId,
                selections[0].referenceBranchId, material),
            recordVolumePlate(bench, fields, selections[1].objectBranchId,
                selections[1].referenceBranchId, material),
            recordVolumePlate(bench, fields, selections[2].objectBranchId,
                selections[2].referenceBranchId, material),
        }},
    };
}

RgbVolumePlateRecordingResult recordRgbReflectionVolumePlate(
    const scene::BenchScene& bench,
    const PlateIncidentFieldSet& fields,
    const std::array<PlateBranchPairSelection, 3>& selections,
    const VolumePlateMaterial& material,
    const PlateFieldSamplingOptions& sampling,
    compute::fft::IFftBackend& fftBackend,
    const ray::ILensPrescriptionResolver* lensPrescriptions,
    const slm::ISlmResponseResolver* slmResponses,
    double environmentTemperatureKelvin) {
    auto result = recordRgbReflectionVolumePlate(
        bench, fields, selections, material);
    for (std::size_t index = 0U; index < result.channels.size(); ++index) {
        result.channels[index] = recordVolumePlate(
            bench,
            fields,
            selections[index].objectBranchId,
            selections[index].referenceBranchId,
            material,
            sampling,
            fftBackend,
            {},
            lensPrescriptions,
            slmResponses,
            environmentTemperatureKelvin);
    }
    return result;
}

RgbVolumePlateReplayResult replayRgbReflectionVolumeToObservation(
    const scene::BenchScene& bench,
    const PlateIncidentFieldSet& fields,
    const RgbVolumePlateRecordingResult& recording,
    std::string observationComponentId,
    const PlateFieldSamplingOptions& sampling,
    compute::fft::IFftBackend& fftBackend,
    const ray::ILensPrescriptionResolver* lensPrescriptions,
    const slm::ISlmResponseResolver* slmResponses,
    double environmentTemperatureKelvin) {
    if (recording.isStaleFor(bench)) {
        throw std::invalid_argument(
            "RGB reflection replay requires a current three-channel volume recording");
    }
    const std::string sharedObservationId = observationComponentId;
    return {
        .plateComponentId = recording.plateComponentId,
        .observationComponentId = std::move(observationComponentId),
        .sourceRevision = recording.sourceRevision,
        .channels = {{
            replayVolumeReflectionToObservation(
                bench, fields, recording.channels[0],
                recording.channels[0].pair.referenceBranchId,
                sharedObservationId, sampling, fftBackend,
                lensPrescriptions,
                slmResponses,
                environmentTemperatureKelvin),
            replayVolumeReflectionToObservation(
                bench, fields, recording.channels[1],
                recording.channels[1].pair.referenceBranchId,
                sharedObservationId, sampling, fftBackend,
                lensPrescriptions,
                slmResponses,
                environmentTemperatureKelvin),
            replayVolumeReflectionToObservation(
                bench, fields, recording.channels[2],
                recording.channels[2].pair.referenceBranchId,
                sharedObservationId, sampling, fftBackend,
                lensPrescriptions,
                slmResponses,
                environmentTemperatureKelvin),
        }},
    };
}

} // namespace holobench::optics::holography
