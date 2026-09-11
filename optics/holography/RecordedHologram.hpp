#pragma once

#include <atomic>
#include <filesystem>
#include <string>

#include "optics/holography/BenchVolumeHologram.hpp"

namespace holobench::optics::holography {

// A detached, plate-local scalar recording. No live scene or source raster is
// required for replay. The shared owner used by the showroom is const.
struct RecordedHologram final {
    std::string sourcePlateId;
    VolumeHologramParameters material;
    math::Vec3d gratingVector;
    double centreXMetres = 0.0;
    double centreYMetres = 0.0;
    field::ComplexField2D coupling;
    // Nominal object-centre depth, only an initial camera accommodation hint.
    // It never replaces or modifies the recorded complex field.
    double suggestedFocusDepthMetres = 0.0;
};

// Retain the physical window and increase the power-of-two grid to resolve
// each incident carrier and their product, with 25% bandwidth headroom.
// This checks centre carriers, not convergence of all object detail.
[[nodiscard]] PlateFieldSamplingOptions reconstructionSampling(
    const scene::BenchScene &scene, const PlateIncidentFieldSet &fields,
    std::uint64_t objectBranch, std::uint64_t referenceBranch,
    PlateFieldSamplingOptions requested);

struct LocalVolumeGratingSample final {
    float amplitude = 0.0f; // normalized modulation amplitude in [0, 1]
    float Kx = 0.0f;        // local 3D volume grating vector X (rad/m)
    float Ky = 0.0f;        // local 3D volume grating vector Y (rad/m)
    float Kz = 0.0f;        // local 3D volume grating vector Z (rad/m)
};

[[nodiscard]] std::vector<LocalVolumeGratingSample>
computeLocalVolumeGratingField(const RecordedHologram &asset);

[[nodiscard]] RecordedHologram
freezePlateRecording(const scene::BenchScene &scene,
                     const VolumePlateRecordingResult &recording);

[[nodiscard]] inline RecordedHologram
freezeReflectionRecording(const scene::BenchScene &scene,
                          const VolumePlateRecordingResult &recording) {
    return freezePlateRecording(scene, recording);
}
void validateRecordedHologram(const RecordedHologram &asset);
// Analytic calibration specimen, explicitly not a measured material or a
// replacement for the user's recording: two coherent paraxial point waves.
[[nodiscard]] RecordedHologram makeTwoPointReflectionReference();
void saveRecordedHologram(const RecordedHologram &asset, const std::filesystem::path &path);
[[nodiscard]] RecordedHologram loadRecordedHologram(const std::filesystem::path &path);
void saveRecordedHolograms(const std::vector<RecordedHologram>& channels, const std::filesystem::path& path);
[[nodiscard]] std::vector<RecordedHologram> loadRecordedHolograms(const std::filesystem::path& path);

struct HologramView final {
    math::RigidTransform3d platePose;
    math::Vec3d illuminationDirection{0.0, 0.0, -1.0};
    double wavelengthMetres = 532e-9;
    double irradianceWattsPerSquareMetre = 1.0;
    // Pupil axes are world X/Y, propagation toward +Z; the eye looks toward -Z.
    math::Vec3d eyePosition{0.0, 0.0, 0.1};
    double pupilRadiusMetres = 0.0005;
    double focusDistanceMetres = 0.1;
    double sensorDistanceMetres = 0.025;
    unsigned paddingFactor = 1;
};

[[nodiscard]] HologramView defaultHologramView(const RecordedHologram &asset);

struct HologramViewResult final {
    field::ComplexField2D sensorField;
    double efficiency = 0.0;
    double exitPowerWatts = 0.0;
    double pupilPowerWatts = 0.0;
    double boundaryPowerFraction = 0.0;
};

// Scalar equivalent-symmetric TE grating, tilted angular spectrum to a finite
// pupil, then a paraxial focused camera. See ADR 0044. No scene revision edits.
[[nodiscard]] HologramViewResult
observeRecordedHologram(const RecordedHologram &asset, const HologramView &view,
                        compute::fft::IFftBackend &fft,
                        const std::atomic_bool *cancelled = nullptr);

} // namespace holobench::optics::holography
