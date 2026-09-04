#include "app/BenchWaveObservationWorker.hpp"

namespace holobench::app {

BenchWaveObservationWorker::BenchWaveObservationWorker()
    : fftBackend_(std::make_unique<compute::fft::CpuFftBackend>())
    , thread_(&BenchWaveObservationWorker::workerLoop, this) {
}

BenchWaveObservationWorker::~BenchWaveObservationWorker() {
    stop();
}

void BenchWaveObservationWorker::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_.store(true, std::memory_order_release);
        cancelRequested_.store(true, std::memory_order_release);
        pendingRequest_.reset();
    }
    cv_.notify_all();
    cvFinished_.notify_all();
    if (thread_.joinable()) {
        thread_.join();
    }
}

void BenchWaveObservationWorker::submitRequest(ObservationWorkerRequest request) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        currentRequestId_.store(request.requestId, std::memory_order_release);
        cancelRequested_.store(true, std::memory_order_release);
        pendingRequest_ = std::move(request);
    }
    cv_.notify_one();
}

void BenchWaveObservationWorker::cancel() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        cancelRequested_.store(true, std::memory_order_release);
        pendingRequest_.reset();
        if (!currentlyComputing_) {
            cvFinished_.notify_all();
        }
    }
}

std::optional<ProgressiveObservationStage> BenchWaveObservationWorker::pollResult() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (completedStage_.has_value()) {
        auto result = std::move(completedStage_);
        completedStage_.reset();
        return result;
    }
    return std::nullopt;
}

bool BenchWaveObservationWorker::isBusy() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return currentlyComputing_ || pendingRequest_.has_value();
}

void BenchWaveObservationWorker::waitForCompletion() {
    std::unique_lock<std::mutex> lock(mutex_);
    cvFinished_.wait(lock, [this]() {
        return !currentlyComputing_ && !pendingRequest_.has_value();
    });
}

std::uint64_t BenchWaveObservationWorker::currentRequestId() const noexcept {
    return currentRequestId_.load(std::memory_order_relaxed);
}

void BenchWaveObservationWorker::workerLoop() {
    while (true) {
        ObservationWorkerRequest request;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this]() {
                return stopping_.load(std::memory_order_relaxed)
                    || pendingRequest_.has_value();
            });
            if (stopping_.load(std::memory_order_relaxed)) {
                break;
            }
            request = std::move(*pendingRequest_);
            pendingRequest_.reset();
            cancelRequested_.store(false, std::memory_order_release);
            currentlyComputing_ = true;
        }

        const std::size_t totalStages = request.stages.size();
        for (std::size_t i = 0; i < totalStages; ++i) {
            if (cancelRequested_.load(std::memory_order_relaxed)
                || stopping_.load(std::memory_order_relaxed)) {
                break;
            }

            const std::size_t sampleLimit = request.stages[i];
            const bool isFinal = (i + 1U == totalStages);
            const bool isPreview = !isFinal;

            try {
                auto channels = observeBenchWaveChannels(
                    request.scene,
                    request.traceGraph,
                    request.observationComponentId,
                    sampleLimit,
                    isPreview,
                    *fftBackend_,
                    request.lensPrescriptions,
                    request.slmResponses,
                    request.environmentTemperatureKelvin,
                    &cancelRequested_);

                if (cancelRequested_.load(std::memory_order_relaxed)
                    || stopping_.load(std::memory_order_relaxed)) {
                    break;
                }

                if (!channels.empty()) {
                    ProgressiveObservationStage stage {
                        .requestId = request.requestId,
                        .sampleLimit = sampleLimit,
                        .stageIndex = i,
                        .totalStages = totalStages,
                        .isFinalStage = isFinal,
                        .channels = std::move(channels),
                        .diagnosticError = {},
                    };
                    {
                        std::lock_guard<std::mutex> lock(mutex_);
                        if (!cancelRequested_.load(std::memory_order_relaxed)
                            && currentRequestId_.load(std::memory_order_relaxed) == request.requestId) {
                            completedStage_ = std::move(stage);
                        }
                    }
                }
            } catch (const optics::wave::OperationCancelledException&) {
                break;
            } catch (const std::exception& error) {
                if (cancelRequested_.load(std::memory_order_relaxed)
                    || stopping_.load(std::memory_order_relaxed)) {
                    break;
                }
                ProgressiveObservationStage stage {
                    .requestId = request.requestId,
                    .sampleLimit = sampleLimit,
                    .stageIndex = i,
                    .totalStages = totalStages,
                    .isFinalStage = true,
                    .channels = {},
                    .diagnosticError = error.what(),
                };
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    if (!cancelRequested_.load(std::memory_order_relaxed)
                        && currentRequestId_.load(std::memory_order_relaxed) == request.requestId) {
                        completedStage_ = std::move(stage);
                    }
                }
                break;
            }
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            currentlyComputing_ = false;
        }
        cvFinished_.notify_all();
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        currentlyComputing_ = false;
    }
    cvFinished_.notify_all();
}

} // namespace holobench::app
