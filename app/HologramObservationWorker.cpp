#include "app/HologramObservationWorker.hpp"

#include "compute/fft/CpuFftBackend.hpp"
#include "core/math/RigidTransform.hpp"
#include <numbers>
#include <stdexcept>

namespace holobench::app {
HologramObservationWorker::HologramObservationWorker() : thread_([this] { run(); }) {}
HologramObservationWorker::~HologramObservationWorker() {
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
        cancelled_ = true;
        pending_.reset();
    }
    cv_.notify_all();
    if (thread_.joinable())
        thread_.join();
}
void HologramObservationWorker::submit(
    std::uint64_t id, std::shared_ptr<const optics::holography::RecordedHologram> asset,
    optics::holography::HologramView view) {
    if (!asset)
        throw std::invalid_argument("Missing recorded hologram");
    {
        std::lock_guard lock(mutex_);
        latest_ = id;
        cancelled_ = true;
        completed_.reset();
        pending_ = Request{id, std::move(asset), {}, view};
        progress_.busy = true;
        progress_.fraction = 0.05F;
        progress_.stageText = "Initializing wavefront reconstruction...";
        progress_.error.clear();
    }
    cv_.notify_one();
}
void HologramObservationWorker::submitMultiChannel(
    std::uint64_t id, std::vector<optics::holography::RecordedHologram> channels,
    optics::holography::HologramView view) {
    if (channels.empty())
        throw std::invalid_argument("Missing recorded holograms for multi-channel observation");
    {
        std::lock_guard lock(mutex_);
        latest_ = id;
        cancelled_ = true;
        completed_.reset();
        auto rep = std::make_shared<optics::holography::RecordedHologram>(channels[0]);
        pending_ = Request{id, std::move(rep), std::move(channels), view};
        progress_.busy = true;
        progress_.fraction = 0.05F;
        progress_.stageText = "Starting RGB reconstruction (3 channels)...";
        progress_.error.clear();
    }
    cv_.notify_one();
}
void HologramObservationWorker::cancel() {
    std::lock_guard lock(mutex_);
    cancelled_ = true;
    pending_.reset();
    completed_.reset();
    progress_.busy = false;
    progress_.stageText = "Cancelled";
    finished_.notify_all();
}
void HologramObservationWorker::wait() {
    std::unique_lock lock(mutex_);
    finished_.wait(lock, [this] { return !busy_ && !pending_; });
}
std::optional<HologramObservationUpdate> HologramObservationWorker::poll() {
    std::lock_guard lock(mutex_);
    auto value = std::move(completed_);
    completed_.reset();
    return value;
}
HologramObservationProgress HologramObservationWorker::progress() const {
    std::lock_guard lock(mutex_);
    return progress_;
}
void HologramObservationWorker::run() {
    compute::fft::CpuFftBackend fft;
    for (;;) {
        std::optional<Request> job;
        {
            std::unique_lock lock(mutex_);
            cv_.wait(lock, [this] { return stopping_ || pending_.has_value(); });
            if (stopping_)
                return;
            job = std::move(pending_);
            pending_.reset();
            cancelled_ = false;
            busy_ = true;
            progress_.busy = true;
            progress_.fraction = 0.10F;
            progress_.error.clear();
        }
        if (!job->channels.empty()) {
            HologramObservationUpdate update;
            update.requestId = job->id;
            update.stage = 1;
            update.finalStage = true;
            try {
                job->view.paddingFactor = 1;
                const std::size_t totalChannels = job->channels.size();
                const double panEyeX = job->view.eyePosition.x;
                const double panEyeY = job->view.eyePosition.y;
                for (std::size_t i = 0; i < totalChannels; ++i) {
                    if (cancelled_)
                        break;
                    const auto &ch = job->channels[i];
                    const double nm = ch.material.recordingVacuumWavelengthMetres * 1e9;
                    {
                        std::lock_guard lock(mutex_);
                        progress_.busy = true;
                        progress_.fraction = static_cast<float>(i) / static_cast<float>(totalChannels);
                        char text[128];
                        std::snprintf(text, sizeof(text), "Reconstructing channel %zu/%zu (%.0f nm)...",
                                      i + 1, totalChannels, nm);
                        progress_.stageText = text;
                    }
                    auto chView = job->view;
                    chView.wavelengthMetres = ch.material.recordingVacuumWavelengthMetres;
                    const double k = 2.0 * std::numbers::pi / chView.wavelengthMetres;
                    const double pitch = std::max(ch.coupling.pitchXMetres(), ch.coupling.pitchYMetres());
                    if (pitch > 0.0 && chView.focusDistanceMetres > 0.0) {
                        const double maxPupil = 0.85 * std::numbers::pi * chView.focusDistanceMetres / (k * pitch);
                        chView.pupilRadiusMetres = std::min(chView.pupilRadiusMetres, maxPupil);
                    }
                    const auto dir = math::transformDirectionWorldToLocal(
                        chView.platePose, chView.illuminationDirection);
                    const double corr = 1.0 / (1.0 - ch.material.isotropicLinearShrinkageFraction) - 1.0;
                    const double qx = k * dir.x + ch.gratingVector.x * (1.0 + corr);
                    const double qy = k * dir.y + ch.gratingVector.y * (1.0 + corr);
                    const bool isReflection = ch.material.geometry == optics::holography::VolumeHologramGeometry::Reflection;
                    const double outZSign = isReflection ? 1.0 : std::copysign(1.0, dir.z);
                    if (qx * qx + qy * qy < k * k) {
                        const math::Vec3d localOut{
                            qx / k, qy / k,
                            outZSign * std::sqrt(1.0 - (qx * qx + qy * qy) / (k * k))};
                        const auto worldOut = math::transformDirectionLocalToWorld(chView.platePose, localOut);
                        if (worldOut.z > 0.0) {
                            const double eyeZ = chView.eyePosition.z;
                            const auto chief = chView.platePose.translationMetres +
                                               worldOut * ((eyeZ - chView.platePose.translationMetres.z) / worldOut.z);
                            chView.eyePosition.x = chief.x + panEyeX;
                            chView.eyePosition.y = chief.y + panEyeY;
                        }
                    }
                    try {
                        update.channelResults.push_back(
                            optics::holography::observeRecordedHologram(ch, chView, fft, &cancelled_));
                    } catch (const std::exception &e) {
                        const std::string_view msg = e.what();
                        if (msg.find("behind the reconstructed hemisphere") != std::string_view::npos ||
                            msg.find("outside the supported translated observation window") != std::string_view::npos) {
                            field::ComplexField2D darkField(
                                ch.coupling.width(), ch.coupling.height(),
                                ch.coupling.pitchXMetres(), ch.coupling.pitchYMetres(),
                                chView.wavelengthMetres);
                            update.channelResults.push_back(optics::holography::HologramViewResult{
                                std::move(darkField), 0.0, 0.0, 0.0, 0.0});
                        } else {
                            throw;
                        }
                    }
                    {
                        std::lock_guard lock(mutex_);
                        progress_.fraction = static_cast<float>(i + 1) / static_cast<float>(totalChannels);
                    }
                }
                if (!update.channelResults.empty()) {
                    update.result = update.channelResults[0];
                }
            } catch (const std::exception &e) {
                update.error = e.what();
            }
            {
                std::lock_guard lock(mutex_);
                busy_ = false;
                if (!cancelled_ && latest_ == job->id) {
                    completed_ = std::move(update);
                    progress_.busy = false;
                    progress_.fraction = 1.0F;
                    if (!completed_->error.empty()) {
                        progress_.error = completed_->error;
                        progress_.stageText = "Error: " + completed_->error;
                    } else {
                        progress_.stageText = "Reconstruction complete";
                    }
                }
            }
            finished_.notify_all();
            continue;
        }
        const unsigned stages =
            job->asset->coupling.width() <= 512U && job->asset->coupling.height() <= 512U ? 2U : 1U;
        const double panEyeX = job->view.eyePosition.x;
        const double panEyeY = job->view.eyePosition.y;
        for (unsigned stage = 1; stage <= stages && !cancelled_; ++stage) {
            {
                std::lock_guard lock(mutex_);
                progress_.busy = true;
                progress_.fraction = static_cast<float>(stage - 1) / static_cast<float>(stages);
                char text[128];
                std::snprintf(text, sizeof(text), (stage == 1 && stages > 1)
                                                  ? "Synthesizing pupil window preview (Stage 1/%u)..."
                                                  : "Wavefield propagation (Stage %u/%u)...",
                              stage, stages);
                progress_.stageText = text;
            }
            HologramObservationUpdate update;
            update.requestId = job->id;
            update.stage = stage;
            update.finalStage = stage == stages;
            try {
                job->view.paddingFactor = stage;
                const auto &asset = *job->asset;
                const double lambda = job->view.wavelengthMetres;
                const double k = 2.0 * std::numbers::pi / lambda;
                const auto dir = math::transformDirectionWorldToLocal(
                    job->view.platePose, job->view.illuminationDirection);
                const double corr = 1.0 / (1.0 - asset.material.isotropicLinearShrinkageFraction) - 1.0;
                const double qx = k * dir.x + asset.gratingVector.x * (1.0 + corr);
                const double qy = k * dir.y + asset.gratingVector.y * (1.0 + corr);
                const bool isReflection = asset.material.geometry == optics::holography::VolumeHologramGeometry::Reflection;
                const double outZSign = isReflection ? 1.0 : std::copysign(1.0, dir.z);
                if (qx * qx + qy * qy < k * k) {
                    const math::Vec3d localOut{
                        qx / k, qy / k,
                        outZSign * std::sqrt(1.0 - (qx * qx + qy * qy) / (k * k))};
                    const auto worldOut = math::transformDirectionLocalToWorld(job->view.platePose, localOut);
                    if (worldOut.z > 0.0) {
                        const double eyeZ = job->view.eyePosition.z;
                        const auto chief = job->view.platePose.translationMetres +
                                           worldOut * ((eyeZ - job->view.platePose.translationMetres.z) / worldOut.z);
                        job->view.eyePosition.x = chief.x + panEyeX;
                        job->view.eyePosition.y = chief.y + panEyeY;
                    }
                }
                try {
                    update.result = optics::holography::observeRecordedHologram(*job->asset, job->view,
                                                                                fft, &cancelled_);
                } catch (const std::exception &e) {
                    const std::string_view msg = e.what();
                    if (msg.find("behind the reconstructed hemisphere") != std::string_view::npos ||
                        msg.find("outside the supported translated observation window") != std::string_view::npos) {
                        field::ComplexField2D darkField(
                            job->asset->coupling.width(), job->asset->coupling.height(),
                            job->asset->coupling.pitchXMetres(), job->asset->coupling.pitchYMetres(),
                            job->view.wavelengthMetres);
                        update.result = optics::holography::HologramViewResult{
                            std::move(darkField), 0.0, 0.0, 0.0, 0.0};
                    } else {
                        throw;
                    }
                }
            } catch (const std::exception &e) {
                update.error = e.what();
            }
            const bool failed = !update.error.empty();
            {
                std::lock_guard lock(mutex_);
                if (!cancelled_ && latest_ == job->id) {
                    completed_ = std::move(update);
                    progress_.fraction = static_cast<float>(stage) / static_cast<float>(stages);
                    if (failed) {
                        progress_.busy = false;
                        progress_.error = completed_->error;
                        progress_.stageText = "Error: " + completed_->error;
                    } else if (stage == stages) {
                        progress_.busy = false;
                        progress_.fraction = 1.0F;
                        progress_.stageText = "Reconstruction complete";
                    }
                }
            }
            if (failed)
                break;
        }
        {
            std::lock_guard lock(mutex_);
            busy_ = false;
        }
        finished_.notify_all();
    }
}
} // namespace holobench::app
