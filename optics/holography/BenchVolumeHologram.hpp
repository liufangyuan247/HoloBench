#pragma once

#include <cstdint>
#include <string>

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

[[nodiscard]] VolumePlateReplayResult replayVolumePlate(
    const scene::BenchScene& bench,
    const VolumePlateRecordingResult& recording,
    double replayVacuumWavelengthMetres,
    double replayAngleInMediumRadians);

} // namespace holobench::optics::holography
