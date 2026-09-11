#pragma once
#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <glad/gl.h>
#include <glm/glm.hpp>

#include "app/HologramObservationWorker.hpp"
#include "render/gl/Framebuffer.hpp"
#include "render/gl/Shader.hpp"
#include "render/gl/Texture2D.hpp"

namespace holobench::app {
enum class ShowroomMode {
    OrbitParallax, // 3D Orbit & Parallax (lamp & plate rotate as a rigid body, constant Bragg resonance, free 3D inspection)
    BraggTuning,   // Bragg Tuning & Angular Selectivity (fixed overhead lamp or adjustable angle, test volume grating detuning)
};

class HologramShowroom final {
  public:
    HologramShowroom();
    ~HologramShowroom();

    HologramShowroom(const HologramShowroom&) = delete;
    HologramShowroom& operator=(const HologramShowroom&) = delete;
    HologramShowroom(HologramShowroom&&) = delete;
    HologramShowroom& operator=(HologramShowroom&&) = delete;

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
    void setMode(ShowroomMode mode) { mode_ = mode; request(); }
    [[nodiscard]] ShowroomMode mode() const noexcept { return mode_; }
    void setShowFringes(bool show) noexcept { showFringes_ = show; }
    [[nodiscard]] bool showFringes() const noexcept { return showFringes_; }
    void setYawPitch(float yaw, float pitch) { yaw_ = yaw; pitch_ = pitch; request(); }
    std::array<float, 2> canvasCentre{};
    std::array<float, 2> backButtonCentre{};
    std::array<float, 2> referenceButtonCentre{};
    std::array<float, 2> rgbButtonCentre{};
    std::array<std::array<float, 2>, 3> channelButtonCentres{};

  private:
    void request();
    void upload();
    void uploadFringeTextures();
    void initGl();
    void destroyGl() noexcept;
    void renderScene3D(int width, int height);

    HologramObservationWorker worker_;
    std::shared_ptr<const optics::holography::RecordedHologram> asset_;
    std::vector<optics::holography::RecordedHologram> channels_;
    int channelIndex_ = 0;
    optics::holography::HologramView view_;
    optics::holography::HologramView initial_;
    std::optional<optics::holography::HologramViewResult> result_;
    std::vector<optics::holography::HologramViewResult> channelResults_;
    render::gl::Texture2D texture_;
    std::array<render::gl::Texture2D, 3> fringeTextures_;
    bool fringeTexturesReady_ = false;
    render::gl::Framebuffer fbo_;
    render::gl::ShaderProgram textureShader_;
    render::gl::ShaderProgram colorShader_;
    GLuint textureVao_ = 0;
    GLuint textureVbo_ = 0;
    GLuint colorVao_ = 0;
    GLuint colorVbo_ = 0;
    bool glInitialized_ = false;

    std::uint64_t requestId_ = 0;
    double referenceIntensity_ = 0.0;
    float displayExposure_ = 1.0F;
    float yaw_ = 0.0F, pitch_ = 0.0F;
    double panX_ = 0.0, panY_ = 0.0;
    double cameraDistance_ = 0.30;
    ShowroomMode mode_ = ShowroomMode::OrbitParallax;
    float lightPitchDeg_ = 45.0F;
    float lightYawDeg_ = 0.0F;
    bool active_ = false, textureCurrent_ = false;
    bool showFringes_ = false;
    std::string status_;
    char path_[512] = "recorded-plate.holo.json";
};
} // namespace holobench::app
