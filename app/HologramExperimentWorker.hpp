#pragma once

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "app/BenchProject.hpp"
#include "core/field/FieldVisualization.hpp"
#include "core/field/RgbFieldVisualization.hpp"
#include "optics/holography/BenchHologramRecording.hpp"
#include "optics/holography/BenchHologramReplay.hpp"
#include "optics/holography/BenchRgbHologram.hpp"
#include "optics/holography/BenchVolumeHologram.hpp"
#include "optics/holography/BenchVolumeHologramReplay.hpp"
#include "optics/scene/BenchScene.hpp"

namespace holobench::compute::fft {
class CpuFftBackend;
}

namespace holobench::app {

enum class HologramJobKind {
    Record,
    Replay,
};

struct HologramRecordJobRequest final {
    std::uint64_t requestId = 0;
    optics::scene::BenchScene scene;
    optics::holography::PlateIncidentFieldSet fields;
    HologramRecordingRecipe recipe;
    std::vector<optics::holography::PlateBranchPairSelection> resolvedSelections;
    const optics::ray::ILensPrescriptionResolver* lensPrescriptions = nullptr;
    const optics::slm::ISlmResponseResolver* slmResponses = nullptr;
    double environmentTemperatureKelvin = 293.15;
};

struct HologramReplayJobRequest final {
    std::uint64_t requestId = 0;
    optics::scene::BenchScene scene;
    optics::scene::BenchTraceGraph traceGraph;
    optics::holography::PlateIncidentFieldSet fields;
    std::string observationComponentId;
    optics::holography::ThinPlateReplayKind thinReplayKind =
        optics::holography::ThinPlateReplayKind::ConjugateReference;
    optics::holography::PlateFieldSamplingOptions sampling;
    const optics::ray::ILensPrescriptionResolver* lensPrescriptions = nullptr;
    const optics::slm::ISlmResponseResolver* slmResponses = nullptr;
    const optics::material::ICoatingResponseResolver* coatingResponses = nullptr;
    double environmentTemperatureKelvin = 293.15;

    // Source recordings (one populated depending on mode)
    std::optional<optics::holography::ThinPlateRecordingResult> thinRecording;
    std::optional<optics::holography::RgbThinPlateRecordingResult> rgbThinRecording;
    std::optional<optics::holography::VolumePlateRecordingResult> volumeRecording;
    std::optional<optics::holography::RgbVolumePlateRecordingResult> rgbVolumeRecording;

    // Display options
    std::array<float, 3> rgbDisplayGains {1.0f, 1.0f, 1.0f};
    float rgbDisplayGamma = 2.2f;
};

struct HologramExperimentJobResult final {
    std::uint64_t requestId = 0;
    HologramJobKind kind = HologramJobKind::Record;
    bool success = false;
    std::string errorMessage;
    std::string statusMessage;
    std::string recipeId;

    // Pre-rendered visualization image on CPU
    std::optional<field::RgbaImage> previewImage;

    // Results
    std::optional<optics::holography::ThinPlateRecordingResult> thinRecording;
    std::optional<optics::holography::RgbThinPlateRecordingResult> rgbThinRecording;
    std::optional<optics::holography::VolumePlateRecordingResult> volumeRecording;
    std::optional<optics::holography::RgbVolumePlateRecordingResult> rgbVolumeRecording;

    std::optional<optics::holography::ThinPlateReplayResult> thinReplay;
    std::optional<optics::holography::RgbThinPlateReplayResult> rgbThinReplay;
    std::optional<optics::holography::VolumePlateObservationReplayResult> volumeReplay;
    std::optional<optics::holography::RgbVolumePlateReplayResult> rgbVolumeReplay;
};

struct HologramExperimentProgress final {
    bool busy = false;
    float fraction = 0.0f;
    std::string stageText;
};

class HologramExperimentWorker final {
public:
    HologramExperimentWorker();
    ~HologramExperimentWorker();

    HologramExperimentWorker(const HologramExperimentWorker&) = delete;
    HologramExperimentWorker& operator=(const HologramExperimentWorker&) = delete;

    void submitRecord(HologramRecordJobRequest request);
    void submitReconstruct(HologramReplayJobRequest request);
    void cancel();
    void stop();
    void waitForCompletion();

    [[nodiscard]] std::optional<HologramExperimentJobResult> pollResult();
    [[nodiscard]] bool isBusy() const noexcept;
    [[nodiscard]] HologramExperimentProgress progress() const;
    [[nodiscard]] std::uint64_t currentRequestId() const noexcept;

private:
    void workerLoop();
    void executeRecordJob(const HologramRecordJobRequest& req);
    void executeReplayJob(const HologramReplayJobRequest& req);
    void setProgress(float fraction, std::string text);

    std::unique_ptr<compute::fft::CpuFftBackend> fftBackend_;
    std::thread thread_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::condition_variable cvFinished_;

    std::atomic_bool stopping_ {false};
    std::atomic_bool cancelRequested_ {false};
    std::atomic_bool currentlyComputing_ {false};
    std::atomic<std::uint64_t> currentRequestId_ {0};

    std::optional<HologramRecordJobRequest> pendingRecordRequest_;
    std::optional<HologramReplayJobRequest> pendingReplayRequest_;
    std::optional<HologramExperimentJobResult> completedResult_;

    HologramExperimentProgress progress_;
};

} // namespace holobench::app
