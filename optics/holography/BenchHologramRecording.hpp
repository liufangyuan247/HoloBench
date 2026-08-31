#pragma once

#include <cstdint>

#include "optics/holography/PlateFieldSampling.hpp"
#include "optics/holography/ThinHologram.hpp"

namespace holobench::optics::holography {

struct ThinPlateRecordingOptions final {
    PlateFieldSamplingOptions sampling;
    // Physical sampled fields use sqrt(W/m^2). The existing thin-hologram
    // response is explicitly relative, so this value defines relative I=1.
    double relativeIntensityReferenceWattsPerSquareMetre = 1.0;
    ThinHologramResponseParameters response {
        .amplitudeBias = 0.1,
        .intensityToAmplitudeGain = 0.2,
        .minimumAmplitudeTransmission = 0.0,
        .maximumAmplitudeTransmission = 1.0,
    };
};

struct ThinPlateRecordingDiagnostics final {
    double fringeFrequencyXCyclesPerMetre = 0.0;
    double fringeFrequencyYCyclesPerMetre = 0.0;
    double fringePeriodMetres = 0.0;
    double nyquistXCyclesPerMetre = 0.0;
    double nyquistYCyclesPerMetre = 0.0;
    double objectPowerOnSampledWindowWatts = 0.0;
    double referencePowerOnSampledWindowWatts = 0.0;
    double relativeIntensityReferenceWattsPerSquareMetre = 1.0;
    bool fringeCarrierSampled = false;
};

struct ThinPlateRecordingResult final {
    std::string plateComponentId;
    scene::SceneRevision sourceRevision = 0;
    PlateRecordingPair pair;
    SampledPlateIncidentField objectIncident;
    SampledPlateIncidentField referenceIncident;
    field::ComplexField2D relativeObjectField;
    field::ComplexField2D relativeReferenceField;
    ThinAmplitudeHologram hologram;
    ThinPlateRecordingDiagnostics diagnostics;

    [[nodiscard]] bool isStaleFor(const scene::BenchScene& bench) const noexcept;
};

// Records the thin transmission response from two branches that physically hit
// the same placed plate. Opposite-side reflection geometry is deliberately
// rejected here and is handled by the volume-reflection path.
[[nodiscard]] ThinPlateRecordingResult recordThinTransmissionPlate(
    const scene::BenchScene& bench,
    const PlateIncidentFieldSet& fields,
    std::uint64_t objectBranchId,
    std::uint64_t referenceBranchId,
    const ThinPlateRecordingOptions& options = {});

} // namespace holobench::optics::holography
