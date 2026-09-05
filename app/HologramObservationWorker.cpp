#include "app/HologramObservationWorker.hpp"

#include "compute/fft/CpuFftBackend.hpp"
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
        pending_ = Request{id, std::move(asset), view};
    }
    cv_.notify_one();
}
void HologramObservationWorker::cancel() {
    std::lock_guard lock(mutex_);
    cancelled_ = true;
    pending_.reset();
    completed_.reset();
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
        }
        const unsigned stages =
            job->asset->coupling.width() <= 512U && job->asset->coupling.height() <= 512U ? 2U : 1U;
        for (unsigned stage = 1; stage <= stages && !cancelled_; ++stage) {
            HologramObservationUpdate update;
            update.requestId = job->id;
            update.stage = stage;
            update.finalStage = stage == stages;
            try {
                job->view.paddingFactor = stage;
                update.result = optics::holography::observeRecordedHologram(*job->asset, job->view,
                                                                            fft, &cancelled_);
            } catch (const std::exception &e) {
                update.error = e.what();
            }
            const bool failed = !update.error.empty();
            {
                std::lock_guard lock(mutex_);
                if (!cancelled_ && latest_ == job->id)
                    completed_ = std::move(update);
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
