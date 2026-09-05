#pragma once
#include <array>

#include "app/HologramObservationWorker.hpp"
#include "render/gl/Texture2D.hpp"

namespace holobench::app {
class HologramShowroom final {
  public:
    void open(optics::holography::RecordedHologram asset);
    void open(std::vector<optics::holography::RecordedHologram> channels);
    // Returns true while the full-workspace showroom is active.
    bool draw();
    void showEmpty();
    [[nodiscard]] bool active() const noexcept { return active_; }
    void waitForObservation() { worker_.wait(); }
    [[nodiscard]] bool hasCurrentImage() const noexcept { return textureCurrent_; }
    [[nodiscard]] std::uint64_t requestSerial() const noexcept { return requestId_; }
    [[nodiscard]] const std::string &diagnostic() const noexcept { return status_; }
    std::array<float, 2> canvasCentre{};
    std::array<float, 2> backButtonCentre{};
    std::array<float, 2> referenceButtonCentre{};
    std::array<std::array<float, 2>, 3> channelButtonCentres{};

  private:
    void request();
    void upload();
    HologramObservationWorker worker_;
    std::shared_ptr<const optics::holography::RecordedHologram> asset_;
    std::vector<optics::holography::RecordedHologram> channels_;
    int channelIndex_ = 0;
    optics::holography::HologramView view_;
    optics::holography::HologramView initial_;
    std::optional<optics::holography::HologramViewResult> result_;
    render::gl::Texture2D texture_;
    std::uint64_t requestId_ = 0;
    double referenceIntensity_ = 0.0;
    float displayExposure_ = 1.0F;
    float yaw_ = 0.0F, pitch_ = 0.0F;
    bool active_ = false, textureCurrent_ = false;
    std::string status_;
    char path_[512] = "recorded-plate.holo.json";
};
} // namespace holobench::app
