#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "app/BenchWaveObservation.hpp"
#include "compute/fft/CpuFftBackend.hpp"
#include "optics/scene/BenchScene.hpp"
#include "optics/scene/BenchInteraction.hpp"

namespace holobench::app {

struct ProgressiveObservationStage final {
    std::uint64_t requestId = 0;
    std::size_t sampleLimit = 0;
    std::size_t stageIndex = 0;
    std::size_t totalStages = 0;
    bool isFinalStage = false;
    std::vector<BenchWaveObservationResult> channels;
    std::string diagnosticError;
};

struct ObservationWorkerRequest final {
    std::uint64_t requestId = 0;
    optics::scene::BenchScene scene;
    optics::scene::BenchTraceGraph traceGraph;
    std::string observationComponentId;
    std::vector<std::size_t> stages;
    const optics::ray::ILensPrescriptionResolver* lensPrescriptions = nullptr;
    const optics::slm::ISlmResponseResolver* slmResponses = nullptr;
    double environmentTemperatureKelvin = 293.15;
};

class BenchWaveObservationWorker final {
public:
    BenchWaveObservationWorker();
    ~BenchWaveObservationWorker();

    BenchWaveObservationWorker(const BenchWaveObservationWorker&) = delete;
    BenchWaveObservationWorker& operator=(const BenchWaveObservationWorker&) = delete;

    // Submits a new observation request. If a prior job is running or queued,
    // it is immediately marked as cancelled.
    void submitRequest(ObservationWorkerRequest request);

    // Cancels any currently computing or pending job immediately.
    void cancel();

    // Polls the latest completed progressive stage. Returns nullopt if no new stage is available.
    [[nodiscard]] std::optional<ProgressiveObservationStage> pollResult();

    // Indicates whether the worker is actively computing.
    [[nodiscard]] bool isBusy() const noexcept;

    // Current request ID being computed or submitted.
    [[nodiscard]] std::uint64_t currentRequestId() const noexcept;

    // Blocks until all pending and currently computing stages for the current request are complete or cancelled.
    void waitForCompletion();

    // Stops the worker thread cleanly. Called automatically by destructor.
    void stop();

private:
    void workerLoop();

    std::unique_ptr<compute::fft::CpuFftBackend> fftBackend_;
    std::thread thread_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::condition_variable cvFinished_;
    std::atomic_bool stopping_ {false};
    std::atomic_bool cancelRequested_ {false};
    bool currentlyComputing_ {false};
    std::atomic<std::uint64_t> currentRequestId_ {0U};

    std::optional<ObservationWorkerRequest> pendingRequest_;
    std::optional<ProgressiveObservationStage> completedStage_;
};

} // namespace holobench::app
