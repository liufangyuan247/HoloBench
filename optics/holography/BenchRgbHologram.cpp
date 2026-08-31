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
    compute::fft::IFftBackend* fftBackend);

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

std::array<PlateBranchPairSelection, 3> selectRgbThinTransmissionPairs(
    const PlateIncidentFieldSet& fields) {
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
            if (pair.geometry != PlateRecordingGeometry::Transmission) {
                throw std::invalid_argument(
                    "RGB thin recording requires every compatible pair to use transmission geometry");
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
            "RGB thin recording requires exactly three unambiguous compatible object/reference pairs");
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
            "RGB thin recording requires three distinct ordered wavelengths");
    }
    return {{
        candidates[0].selection,
        candidates[1].selection,
        candidates[2].selection,
    }};
}

RgbThinPlateRecordingResult recordRgbThinTransmissionPlate(
    const scene::BenchScene& bench,
    const PlateIncidentFieldSet& fields,
    const std::array<PlateBranchPairSelection, 3>& selections,
    const ThinPlateRecordingOptions& options) {
    return recordRgbThinTransmissionPlateImpl(
        bench, fields, selections, options, nullptr);
}

RgbThinPlateRecordingResult recordRgbThinTransmissionPlate(
    const scene::BenchScene& bench,
    const PlateIncidentFieldSet& fields,
    const std::array<PlateBranchPairSelection, 3>& selections,
    const ThinPlateRecordingOptions& options,
    compute::fft::IFftBackend& fftBackend) {
    return recordRgbThinTransmissionPlateImpl(
        bench, fields, selections, options, &fftBackend);
}

namespace {

RgbThinPlateRecordingResult recordRgbThinTransmissionPlateImpl(
    const scene::BenchScene& bench,
    const PlateIncidentFieldSet& fields,
    const std::array<PlateBranchPairSelection, 3>& selections,
    const ThinPlateRecordingOptions& options,
    compute::fft::IFftBackend* fftBackend) {
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
                *fftBackend);
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

} // namespace holobench::optics::holography
