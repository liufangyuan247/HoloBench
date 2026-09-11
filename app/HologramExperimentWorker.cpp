#include "app/HologramExperimentWorker.hpp"

#include <cmath>
#include <algorithm>

#include "compute/fft/CpuFftBackend.hpp"
#include "optics/wave/BeamFollowingField.hpp"

namespace holobench::app {

HologramExperimentWorker::HologramExperimentWorker()
    : fftBackend_(std::make_unique<compute::fft::CpuFftBackend>())
    , thread_(&HologramExperimentWorker::workerLoop, this) {
}

HologramExperimentWorker::~HologramExperimentWorker() {
    stop();
}

void HologramExperimentWorker::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_.store(true, std::memory_order_release);
        cancelRequested_.store(true, std::memory_order_release);
        pendingRecordRequest_.reset();
        pendingReplayRequest_.reset();
    }
    cv_.notify_all();
    cvFinished_.notify_all();
    if (thread_.joinable()) {
        thread_.join();
    }
}

void HologramExperimentWorker::submitRecord(HologramRecordJobRequest request) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        currentRequestId_.store(request.requestId, std::memory_order_release);
        cancelRequested_.store(true, std::memory_order_release);
        pendingReplayRequest_.reset();
        pendingRecordRequest_ = std::move(request);
        progress_ = {
            .busy = true,
            .fraction = 0.0F,
            .stageText = "Queued recording...",
        };
    }
    cv_.notify_one();
}

void HologramExperimentWorker::submitReconstruct(HologramReplayJobRequest request) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        currentRequestId_.store(request.requestId, std::memory_order_release);
        cancelRequested_.store(true, std::memory_order_release);
        pendingRecordRequest_.reset();
        pendingReplayRequest_ = std::move(request);
        progress_ = {
            .busy = true,
            .fraction = 0.0F,
            .stageText = "Queued reconstruction...",
        };
    }
    cv_.notify_one();
}

void HologramExperimentWorker::cancel() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        cancelRequested_.store(true, std::memory_order_release);
        pendingRecordRequest_.reset();
        pendingReplayRequest_.reset();
        progress_ = {
            .busy = false,
            .fraction = 0.0F,
            .stageText = "Cancelled",
        };
        if (!currentlyComputing_) {
            cvFinished_.notify_all();
        }
    }
}

void HologramExperimentWorker::waitForCompletion() {
    std::unique_lock<std::mutex> lock(mutex_);
    cvFinished_.wait(lock, [this]() {
        return !currentlyComputing_
            && !pendingRecordRequest_.has_value()
            && !pendingReplayRequest_.has_value();
    });
}

std::optional<HologramExperimentJobResult> HologramExperimentWorker::pollResult() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (completedResult_.has_value()) {
        auto result = std::move(completedResult_);
        completedResult_.reset();
        return result;
    }
    return std::nullopt;
}

bool HologramExperimentWorker::isBusy() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return currentlyComputing_
        || pendingRecordRequest_.has_value()
        || pendingReplayRequest_.has_value();
}

HologramExperimentProgress HologramExperimentWorker::progress() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return progress_;
}

std::uint64_t HologramExperimentWorker::currentRequestId() const noexcept {
    return currentRequestId_.load(std::memory_order_relaxed);
}

void HologramExperimentWorker::setProgress(float fraction, std::string text) {
    std::lock_guard<std::mutex> lock(mutex_);
    progress_ = {
        .busy = true,
        .fraction = std::clamp(fraction, 0.0F, 1.0F),
        .stageText = std::move(text),
    };
}

void HologramExperimentWorker::workerLoop() {
    while (true) {
        std::optional<HologramRecordJobRequest> recordReq;
        std::optional<HologramReplayJobRequest> replayReq;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this]() {
                return stopping_.load(std::memory_order_relaxed)
                    || pendingRecordRequest_.has_value()
                    || pendingReplayRequest_.has_value();
            });
            if (stopping_.load(std::memory_order_relaxed)) {
                break;
            }
            if (pendingRecordRequest_.has_value()) {
                recordReq = std::move(pendingRecordRequest_);
                pendingRecordRequest_.reset();
            } else if (pendingReplayRequest_.has_value()) {
                replayReq = std::move(pendingReplayRequest_);
                pendingReplayRequest_.reset();
            }
            cancelRequested_.store(false, std::memory_order_release);
            currentlyComputing_ = true;
        }

        if (recordReq.has_value()) {
            executeRecordJob(*recordReq);
        } else if (replayReq.has_value()) {
            executeReplayJob(*replayReq);
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            currentlyComputing_ = false;
            progress_.busy = false;
        }
        cvFinished_.notify_all();
    }
}

void HologramExperimentWorker::executeRecordJob(const HologramRecordJobRequest& req) {
    if (cancelRequested_.load(std::memory_order_relaxed)
        || stopping_.load(std::memory_order_relaxed)) {
        return;
    }

    HologramExperimentJobResult result;
    result.requestId = req.requestId;
    result.kind = HologramJobKind::Record;
    result.recipeId = req.recipe.recipeId;

    try {
        auto sampling = req.recipe.sampling;
        sampling.cancellationRequested = &cancelRequested_;

        if (req.recipe.model == HologramRecordingModel::ThinTransmission) {
            optics::holography::ThinPlateRecordingOptions thinOptions;
            thinOptions.sampling = sampling;
            thinOptions.relativeIntensityReferenceWattsPerSquareMetre
                = req.recipe.relativeIntensityReferenceWattsPerSquareMetre;
            thinOptions.response = req.recipe.thinResponse;

            if (req.resolvedSelections.size() == 1U) {
                setProgress(0.1F, "Sampling thin transmission fields...");
                if (cancelRequested_.load(std::memory_order_relaxed)) return;

                auto recording = optics::holography::recordThinTransmissionPlate(
                    req.scene,
                    req.fields,
                    req.resolvedSelections[0].objectBranchId,
                    req.resolvedSelections[0].referenceBranchId,
                    thinOptions,
                    *fftBackend_,
                    req.lensPrescriptions,
                    req.slmResponses,
                    req.environmentTemperatureKelvin);

                setProgress(0.9F, "Rendering preview...");
                if (cancelRequested_.load(std::memory_order_relaxed)) return;

                field::FieldVisualizationOptions viewOptions;
                viewOptions.colormap = field::ColormapKind::Inferno;
                result.previewImage = field::renderLinearIntensity(
                    recording.hologram.recordedRelativeIntensity, viewOptions);
                result.thinRecording = std::move(recording);
                result.success = true;
                result.statusMessage = "Recorded thin hologram " + req.recipe.recipeId;
            } else if (req.resolvedSelections.size() == 3U) {
                std::optional<optics::holography::ThinPlateRecordingResult> ch0, ch1, ch2;
                for (std::size_t i = 0; i < 3U; ++i) {
                    if (cancelRequested_.load(std::memory_order_relaxed)) return;
                    setProgress(
                        static_cast<float>(i) / 3.0F,
                        "Recording RGB channel " + std::to_string(i + 1) + "/3...");
                    auto ch = optics::holography::recordThinTransmissionPlate(
                        req.scene,
                        req.fields,
                        req.resolvedSelections[i].objectBranchId,
                        req.resolvedSelections[i].referenceBranchId,
                        thinOptions,
                        *fftBackend_,
                        req.lensPrescriptions,
                        req.slmResponses,
                        req.environmentTemperatureKelvin);
                    if (i == 0) ch0 = std::move(ch);
                    else if (i == 1) ch1 = std::move(ch);
                    else ch2 = std::move(ch);
                }
                if (cancelRequested_.load(std::memory_order_relaxed)) return;

                optics::holography::RgbThinPlateRecordingResult recording {
                    .plateComponentId = req.fields.plateComponentId,
                    .sourceRevision = req.fields.sourceRevision,
                    .channels = {std::move(*ch0), std::move(*ch1), std::move(*ch2)},
                };
                result.rgbThinRecording = std::move(recording);
                result.success = true;
                result.statusMessage = "Recorded RGB thin hologram " + req.recipe.recipeId;
            }
        } else {
            // Volume Grating
            if (req.resolvedSelections.size() == 1U) {
                setProgress(0.2F, "Recording volume plate...");
                if (cancelRequested_.load(std::memory_order_relaxed)) return;

                auto recording = optics::holography::recordVolumePlate(
                    req.scene,
                    req.fields,
                    req.resolvedSelections[0].objectBranchId,
                    req.resolvedSelections[0].referenceBranchId,
                    req.recipe.volumeMaterial,
                    sampling,
                    *fftBackend_,
                    {},
                    req.lensPrescriptions,
                    req.slmResponses,
                    req.environmentTemperatureKelvin);

                result.volumeRecording = std::move(recording);
                result.success = true;
                result.statusMessage = "Recorded volume reflection hologram " + req.recipe.recipeId;
            } else if (req.resolvedSelections.size() == 3U) {
                std::optional<optics::holography::VolumePlateRecordingResult> ch0, ch1, ch2;
                for (std::size_t i = 0; i < 3U; ++i) {
                    if (cancelRequested_.load(std::memory_order_relaxed)) return;
                    setProgress(
                        static_cast<float>(i) / 3.0F,
                        "Recording RGB volume channel " + std::to_string(i + 1) + "/3...");
                    auto ch = optics::holography::recordVolumePlate(
                        req.scene,
                        req.fields,
                        req.resolvedSelections[i].objectBranchId,
                        req.resolvedSelections[i].referenceBranchId,
                        req.recipe.volumeMaterial,
                        sampling,
                        *fftBackend_,
                        {},
                        req.lensPrescriptions,
                        req.slmResponses,
                        req.environmentTemperatureKelvin);
                    if (i == 0) ch0 = std::move(ch);
                    else if (i == 1) ch1 = std::move(ch);
                    else ch2 = std::move(ch);
                }
                if (cancelRequested_.load(std::memory_order_relaxed)) return;

                optics::holography::RgbVolumePlateRecordingResult recording {
                    .plateComponentId = req.fields.plateComponentId,
                    .sourceRevision = req.fields.sourceRevision,
                    .channels = {std::move(*ch0), std::move(*ch1), std::move(*ch2)},
                };
                result.rgbVolumeRecording = std::move(recording);
                result.success = true;
                result.statusMessage = "Recorded RGB volume hologram " + req.recipe.recipeId;
            }
        }
    } catch (const optics::wave::OperationCancelledException&) {
        return;
    } catch (const std::exception& error) {
        if (cancelRequested_.load(std::memory_order_relaxed)) return;
        result.success = false;
        result.errorMessage = "Bench recording failed: " + std::string(error.what());
    }

    if (!cancelRequested_.load(std::memory_order_relaxed)) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (currentRequestId_.load(std::memory_order_relaxed) == req.requestId) {
            completedResult_ = std::move(result);
        }
    }
}

void HologramExperimentWorker::executeReplayJob(const HologramReplayJobRequest& req) {
    if (cancelRequested_.load(std::memory_order_relaxed)
        || stopping_.load(std::memory_order_relaxed)) {
        return;
    }

    HologramExperimentJobResult result;
    result.requestId = req.requestId;
    result.kind = HologramJobKind::Replay;

    try {
        if (req.thinRecording.has_value()) {
            setProgress(0.3F, "Reconstructing thin hologram...");
            if (cancelRequested_.load(std::memory_order_relaxed)) return;

            auto replay = optics::holography::replayThinTransmissionToObservation(
                req.scene,
                *req.thinRecording,
                req.observationComponentId,
                req.thinReplayKind,
                *fftBackend_);

            if (cancelRequested_.load(std::memory_order_relaxed)) return;
            field::FieldVisualizationOptions viewOptions;
            viewOptions.colormap = field::ColormapKind::Inferno;
            result.previewImage = field::renderLinearIntensity(
                replay.fullReplayAtObservation, viewOptions);
            result.thinReplay = std::move(replay);
            result.success = true;
            result.statusMessage = "Reconstructed thin hologram on " + req.observationComponentId;
        } else if (req.rgbThinRecording.has_value()) {
            setProgress(0.3F, "Reconstructing RGB thin hologram...");
            if (cancelRequested_.load(std::memory_order_relaxed)) return;

            auto replay = optics::holography::replayRgbThinTransmissionToObservation(
                req.scene,
                *req.rgbThinRecording,
                req.observationComponentId,
                req.thinReplayKind,
                *fftBackend_);

            if (cancelRequested_.load(std::memory_order_relaxed)) return;
            const field::RgbIntensityVisualizationOptions displayOptions {
                .channelIntensityGains = {
                    static_cast<double>(req.rgbDisplayGains[0]),
                    static_cast<double>(req.rgbDisplayGains[1]),
                    static_cast<double>(req.rgbDisplayGains[2]),
                },
                .referenceIntensity = 0.0,
                .displayGamma = static_cast<double>(req.rgbDisplayGamma),
            };
            result.previewImage = field::renderUncalibratedRgbIntensity(
                replay.channels[0].fullReplayAtObservation,
                replay.channels[1].fullReplayAtObservation,
                replay.channels[2].fullReplayAtObservation,
                displayOptions);
            result.rgbThinReplay = std::move(replay);
            result.success = true;
            result.statusMessage = "Reconstructed three independent RGB channels on "
                + req.observationComponentId;
        } else if (req.volumeRecording.has_value()) {
            setProgress(0.3F, "Reconstructing volume reflection hologram...");
            if (cancelRequested_.load(std::memory_order_relaxed)) return;

            auto sampling = req.sampling;
            sampling.cancellationRequested = &cancelRequested_;
            auto replay = optics::holography::replayVolumeReflectionToObservation(
                req.scene,
                req.fields,
                *req.volumeRecording,
                req.volumeRecording->pair.referenceBranchId,
                req.observationComponentId,
                sampling,
                *fftBackend_,
                req.lensPrescriptions,
                req.slmResponses,
                req.coatingResponses,
                req.environmentTemperatureKelvin);

            if (cancelRequested_.load(std::memory_order_relaxed)) return;
            field::FieldVisualizationOptions viewOptions;
            viewOptions.colormap = field::ColormapKind::Inferno;
            result.previewImage = field::renderLinearIntensity(
                replay.reconstructedAtObservation, viewOptions);
            result.volumeReplay = std::move(replay);
            result.success = true;
            result.statusMessage = "Reconstructed volume hologram on " + req.observationComponentId;
        } else if (req.rgbVolumeRecording.has_value()) {
            std::optional<optics::holography::VolumePlateObservationReplayResult> ch0, ch1, ch2;
            auto sampling = req.sampling;
            sampling.cancellationRequested = &cancelRequested_;
            for (std::size_t i = 0; i < 3U; ++i) {
                if (cancelRequested_.load(std::memory_order_relaxed)) return;
                setProgress(
                    static_cast<float>(i) / 3.0F,
                    "Reconstructing RGB volume channel " + std::to_string(i + 1) + "/3...");
                auto ch = optics::holography::replayVolumeReflectionToObservation(
                    req.scene,
                    req.fields,
                    req.rgbVolumeRecording->channels[i],
                    req.rgbVolumeRecording->channels[i].pair.referenceBranchId,
                    req.observationComponentId,
                    sampling,
                    *fftBackend_,
                    req.lensPrescriptions,
                    req.slmResponses,
                    req.coatingResponses,
                    req.environmentTemperatureKelvin);
                if (i == 0) ch0 = std::move(ch);
                else if (i == 1) ch1 = std::move(ch);
                else ch2 = std::move(ch);
            }
            if (cancelRequested_.load(std::memory_order_relaxed)) return;

            optics::holography::RgbVolumePlateReplayResult replay {
                .plateComponentId = req.rgbVolumeRecording->plateComponentId,
                .observationComponentId = req.observationComponentId,
                .sourceRevision = req.rgbVolumeRecording->sourceRevision,
                .channels = {std::move(*ch0), std::move(*ch1), std::move(*ch2)},
            };

            const field::RgbIntensityVisualizationOptions displayOptions {
                .channelIntensityGains = {
                    static_cast<double>(req.rgbDisplayGains[0]),
                    static_cast<double>(req.rgbDisplayGains[1]),
                    static_cast<double>(req.rgbDisplayGains[2]),
                },
                .referenceIntensity = 0.0,
                .displayGamma = static_cast<double>(req.rgbDisplayGamma),
            };
            result.previewImage = field::renderUncalibratedRgbIntensity(
                replay.channels[0].reconstructedAtObservation,
                replay.channels[1].reconstructedAtObservation,
                replay.channels[2].reconstructedAtObservation,
                displayOptions);
            result.rgbVolumeReplay = std::move(replay);
            result.success = true;
            result.statusMessage = "Reconstructed three independent RGB reflection channels on "
                + req.observationComponentId;
        }
    } catch (const optics::wave::OperationCancelledException&) {
        return;
    } catch (const std::exception& error) {
        if (cancelRequested_.load(std::memory_order_relaxed)) return;
        result.success = false;
        result.errorMessage = "Bench reconstruction failed: " + std::string(error.what());
    }

    if (!cancelRequested_.load(std::memory_order_relaxed)) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (currentRequestId_.load(std::memory_order_relaxed) == req.requestId) {
            completedResult_ = std::move(result);
        }
    }
}

} // namespace holobench::app
