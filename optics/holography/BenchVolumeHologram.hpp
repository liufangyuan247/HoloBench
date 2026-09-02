#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "optics/holography/PlateFieldSampling.hpp"
#include "optics/holography/PlateIncidentFields.hpp"
#include "optics/holography/VolumeHologram.hpp"

namespace holobench::optics::holography {

struct VolumePlateMaterial final {
    double averageRefractiveIndex = 1.5;
    double refractiveIndexModulation = 0.01;
    double isotropicLinearShrinkageFraction = 0.0;

    bool operator==(const VolumePlateMaterial&) const = default;
};

struct VolumePlateRecordingResult final {
    std::string plateComponentId;
    scene::SceneRevision sourceRevision = 0;
    PlateRecordingPair pair;
    math::Vec3d objectDirectionInMediumLocal {};
    math::Vec3d referenceDirectionInMediumLocal {};
    math::Vec3d recordedGratingVectorLocalRadiansPerMetre {};
    double recordedGratingPeriodMetres = 0.0;
    double gratingSlantFromPlateNormalRadians = 0.0;
    double equivalentSymmetricBraggAngleInMediumRadians = 0.0;
    VolumePlateMaterial material;
    VolumeHologramParameters nominalReplayParameters;
    VolumeHologramResult nominalReplay;
    std::optional<SampledPlateIncidentField> objectIncident;
    std::optional<SampledPlateIncidentField> referenceIncident;

    [[nodiscard]] bool isStaleFor(const scene::BenchScene& bench) const noexcept;
};

struct VolumePlateReplayResult final {
    std::string plateComponentId;
    scene::SceneRevision sourceRevision = 0;
    double replayVacuumWavelengthMetres = 532e-9;
    double replayAngleInMediumRadians = 0.0;
    VolumeHologramResult volume;

    [[nodiscard]] bool isStaleFor(const scene::BenchScene& bench) const noexcept;
};

// Records the complete local grating vector k_object-k_reference. The existing
// scalar two-wave solver is evaluated through the equivalent symmetric Bragg
// angle derived from its magnitude; grating slant remains explicit and is not
// falsely claimed to be handled by that one-dimensional efficiency model.
[[nodiscard]] VolumePlateRecordingResult recordVolumePlate(
    const scene::BenchScene& bench,
    const PlateIncidentFieldSet& fields,
    std::uint64_t objectBranchId,
    std::uint64_t referenceBranchId,
    const VolumePlateMaterial& material = {});

// Product recording retains the exact sampled object/reference fields that
// wrote the volume grating. Every supported placed wave transform must be
// applied by the shared beam-following service; an unresolved real-lens
// prescription or other unsupported element rejects the complete recording.
[[nodiscard]] VolumePlateRecordingResult recordVolumePlate(
    const scene::BenchScene& bench,
    const PlateIncidentFieldSet& fields,
    std::uint64_t objectBranchId,
    std::uint64_t referenceBranchId,
    const VolumePlateMaterial& material,
    const PlateFieldSamplingOptions& sampling,
    compute::fft::IFftBackend& fftBackend,
    std::span<const PlacedSlmSparseCommand> slmCommands = {},
    const ray::ILensPrescriptionResolver* lensPrescriptions = nullptr,
    const slm::ISlmResponseResolver* slmResponses = nullptr,
    double environmentTemperatureKelvin = 293.15);

// Automation may already own the exact sampled fields (for example after a
// bounded SLM exposure pass). This adapter validates that both fields are
// current, branch-matched, co-sampled external-plane evidence before attaching
// them to the same volume-recording contract. It preserves a false
// carrierSampled diagnostic when an automation preview intentionally cannot
// resolve the optical carrier; direct field replay continues to reject that
// evidence rather than treating it as a resolved reconstruction.
[[nodiscard]] VolumePlateRecordingResult recordVolumePlateFromSampledFields(
    const scene::BenchScene& bench,
    const PlateIncidentFieldSet& fields,
    std::uint64_t objectBranchId,
    std::uint64_t referenceBranchId,
    const VolumePlateMaterial& material,
    SampledPlateIncidentField objectIncident,
    SampledPlateIncidentField referenceIncident);

[[nodiscard]] VolumePlateReplayResult replayVolumePlate(
    const scene::BenchScene& bench,
    const VolumePlateRecordingResult& recording,
    double replayVacuumWavelengthMetres,
    double replayAngleInMediumRadians);

} // namespace holobench::optics::holography
