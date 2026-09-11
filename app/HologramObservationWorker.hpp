#pragma once

#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

#include "optics/holography/RecordedHologram.hpp"

namespace holobench::app {
struct HologramObservationProgress final {
    bool busy = false;
    float fraction = 1.0F;
    std::string stageText;
    std::string error;
};

struct HologramObservationUpdate final {
    std::uint64_t requestId = 0;
    unsigned stage = 0;
    bool finalStage = false;
    std::optional<optics::holography::HologramViewResult> result;
    std::vector<optics::holography::HologramViewResult> channelResults;
    std::string error;
};

class HologramObservationWorker final {
  public:
    HologramObservationWorker();
    ~HologramObservationWorker();
    HologramObservationWorker(const HologramObservationWorker &) = delete;
    HologramObservationWorker &operator=(const HologramObservationWorker &) = delete;
    void submit(std::uint64_t id, std::shared_ptr<const optics::holography::RecordedHologram> asset,
                optics::holography::HologramView view);
    void submitMultiChannel(std::uint64_t id, std::vector<optics::holography::RecordedHologram> channels,
                            optics::holography::HologramView view);
    void cancel();
    void wait();
    [[nodiscard]] std::optional<HologramObservationUpdate> poll();
    [[nodiscard]] HologramObservationProgress progress() const;

  private:
    struct Request {
        std::uint64_t id;
        std::shared_ptr<const optics::holography::RecordedHologram> asset;
        std::vector<optics::holography::RecordedHologram> channels;
        optics::holography::HologramView view;
    };
    void run();
    mutable std::mutex mutex_;
    std::condition_variable cv_, finished_;
    bool stopping_ = false, busy_ = false;
    std::atomic_bool cancelled_{false};
    std::uint64_t latest_ = 0;
    std::optional<Request> pending_;
    std::optional<HologramObservationUpdate> completed_;
    HologramObservationProgress progress_;
    // Last member: the thread cannot see partly initialized state.
    std::thread thread_;
};
} // namespace holobench::app
