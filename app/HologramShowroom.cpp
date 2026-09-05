#include "app/HologramShowroom.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <imgui.h>
#include <numbers>

namespace holobench::app {
namespace h = optics::holography;
void HologramShowroom::showEmpty() {
    worker_.cancel();
    ++requestId_;
    asset_.reset(); channels_.clear(); result_.reset();
    textureCurrent_ = false;
    status_ = "Record the selected plate or open a saved recording";
    active_ = true;
}
namespace {
math::Vec3d rotate(math::Vec3d v, double yaw, double pitch) {
    const math::Vec3d p{v.x, std::cos(pitch) * v.y - std::sin(pitch) * v.z,
                        std::sin(pitch) * v.y + std::cos(pitch) * v.z};
    return {std::cos(yaw) * p.x + std::sin(yaw) * p.z, p.y,
            -std::sin(yaw) * p.x + std::cos(yaw) * p.z};
}
} // namespace
void HologramShowroom::open(h::RecordedHologram asset) {
    channels_.clear();
    auto candidate = std::make_shared<const h::RecordedHologram>(std::move(asset));
    const auto initial = h::defaultHologramView(*candidate);
    asset_ = std::move(candidate);
    view_ = initial_ = initial;
    referenceIntensity_ = 0.0;
    displayExposure_ = 1.0F;
    yaw_ = pitch_ = 0.0F;
    active_ = true;
    request();
}
void HologramShowroom::open(std::vector<h::RecordedHologram> channels) {
    if (channels.empty()) throw std::invalid_argument("No recorded wavelength channels");
    if (channels.size() > 3U) throw std::invalid_argument("At most three recorded wavelength channels are supported");
    for (const auto& channel : channels) h::validateRecordedHologram(channel);
    open(channels.front());
    channels_ = std::move(channels);
    channelIndex_ = 0;
}
void HologramShowroom::request() {
    if (!asset_)
        return;
    view_.platePose.localXAxisInWorld = rotate(initial_.platePose.localXAxisInWorld, yaw_, pitch_);
    view_.platePose.localYAxisInWorld = rotate(initial_.platePose.localYAxisInWorld, yaw_, pitch_);
    view_.platePose.localZAxisInWorld = rotate(initial_.platePose.localZAxisInWorld, yaw_, pitch_);
    textureCurrent_ = false;
    result_.reset();
    status_ = "Computing current view...";
    worker_.submit(++requestId_, asset_, view_);
}
void HologramShowroom::upload() {
    if (!result_)
        return;
    const auto &f = result_->sensorField;
    if (referenceIntensity_ <= 0.0) {
        for (const auto &s : f.samples())
            referenceIntensity_ = std::max(referenceIntensity_, std::norm(s));
    }
    const double reference = std::max(referenceIntensity_, 1e-20);
    std::vector<std::uint8_t> pixels(f.sampleCount() * 4U, 255U);
    for (std::size_t i = 0; i < f.sampleCount(); ++i) {
        const double value =
            std::pow(std::clamp(std::norm(f.samples()[i]) * static_cast<double>(displayExposure_) /
                                    reference,
                                0.0, 1.0),
                     1.0 / 2.2);
        const auto byte = static_cast<std::uint8_t>(std::lround(255.0 * value));
        pixels[i * 4U] = pixels[i * 4U + 1U] = pixels[i * 4U + 2U] = byte;
    }
    textureCurrent_ =
        texture_.uploadRgba8(static_cast<int>(f.width()), static_cast<int>(f.height()), pixels);
    if (!textureCurrent_)
        status_ = "Could not upload observer image";
}
bool HologramShowroom::draw() {
    if (!active_)
        return false;
    const auto *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 1));
    ImGui::Begin("Hologram showroom", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    if (ImGui::Button("Back to Bench")) {
        active_ = false;
        worker_.cancel();
    }
    backButtonCentre = {(ImGui::GetItemRectMin().x + ImGui::GetItemRectMax().x) * 0.5F,
                        (ImGui::GetItemRectMin().y + ImGui::GetItemRectMax().y) * 0.5F};
    ImGui::SameLine();
    ImGui::TextUnformatted("HOLOGRAM SHOWROOM  |  Monochromatic scalar reconstruction");
    if (ImGui::Button("Recorded plate file"))
        ImGui::OpenPopup("Showroom file");
    ImGui::SameLine();
    if (ImGui::Button("Two-depth reference")) {
        open(h::makeTwoPointReflectionReference());
        view_.focusDistanceMetres = 0.14;
        initial_ = view_;
        request();
    }
    referenceButtonCentre = {(ImGui::GetItemRectMin().x + ImGui::GetItemRectMax().x) * 0.5F,
                             (ImGui::GetItemRectMin().y + ImGui::GetItemRectMax().y) * 0.5F};
    if (ImGui::BeginPopup("Showroom file")) {
        ImGui::InputText("Path", path_, sizeof(path_));
        if (ImGui::Button("Open")) {
            try {
                open(h::loadRecordedHolograms(path_));
            } catch (const std::exception &e) {
                status_ = e.what();
            }
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!asset_);
        if (ImGui::Button("Save recording")) {
            try {
                if (channels_.empty()) h::saveRecordedHologram(*asset_, path_);
                else h::saveRecordedHolograms(channels_, path_);
                status_ = "Detached recording saved";
            } catch (const std::exception &e) {
                status_ = e.what();
            }
        }
        ImGui::EndDisabled();
        ImGui::EndPopup();
    }
    bool changed = false;
    if (channels_.size() > 1U) {
        ImGui::TextUnformatted("Recorded wavelength:");
        for (std::size_t i = 0; i < channels_.size(); ++i) {
            ImGui::SameLine();
            const auto label = std::to_string(static_cast<int>(std::lround(
                channels_[i].material.recordingVacuumWavelengthMetres * 1e9))) + " nm";
            if (ImGui::RadioButton(label.c_str(), channelIndex_ == static_cast<int>(i))) {
                auto channels = std::move(channels_);
                open(channels[i]);
                channels_ = std::move(channels);
                channelIndex_ = static_cast<int>(i);
            }
            channelButtonCentres[i] = {(ImGui::GetItemRectMin().x + ImGui::GetItemRectMax().x) * 0.5F,
                (ImGui::GetItemRectMin().y + ImGui::GetItemRectMax().y) * 0.5F};
        }
    }
    if (asset_) {
        ImGui::SameLine();
        bool lit = view_.irradianceWattsPerSquareMetre > 0.0;
        if (ImGui::Checkbox("Light", &lit)) {
            view_.irradianceWattsPerSquareMetre = lit ? 1.0 : 0.0;
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset pose / light")) {
            view_ = initial_;
            yaw_ = pitch_ = 0;
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Observation settings"))
            ImGui::OpenPopup("Observation settings");
        if (ImGui::BeginPopup("Observation settings")) {
            changed |= ImGui::SliderFloat("Plate yaw (rad)", &yaw_, -0.25F, 0.25F, "%.4f");
            changed |= ImGui::SliderFloat("Plate pitch (rad)", &pitch_, -0.25F, 0.25F, "%.4f");
            const double small = 0.00001;
            changed |=
                ImGui::InputDouble("Eye X (m)", &view_.eyePosition.x, small, small * 10, "%.6f");
            changed |=
                ImGui::InputDouble("Eye Y (m)", &view_.eyePosition.y, small, small * 10, "%.6f");
            changed |=
                ImGui::InputDouble("Eye distance (m)", &view_.eyePosition.z, 0.005, 0.01, "%.4f");
            changed |= ImGui::InputDouble("Focus distance (m)", &view_.focusDistanceMetres, 0.005,
                                          0.01, "%.4f");
            changed |= ImGui::InputDouble("Pupil radius (m)", &view_.pupilRadiusMetres, small,
                                          small * 10, "%.6f");
            double nm = view_.wavelengthMetres * 1e9;
            if (ImGui::InputDouble("Wavelength (nm)", &nm, 0.1, 1.0, "%.2f")) {
                view_.wavelengthMetres = nm * 1e-9;
                changed = true;
            }
            if (ImGui::SliderFloat("Display exposure (locked scale)", &displayExposure_, 0.01F,
                                   100.0F, "%.2f", ImGuiSliderFlags_Logarithmic))
                upload();
            ImGui::TextWrapped(
                "Finite sampled window; equivalent-symmetric TE grating; paraxial focused camera. "
                "Grayscale intensity display. White-light colour is not yet supported.");
            ImGui::EndPopup();
        }
    }
    if (asset_)
        ImGui::TextDisabled("%s | %.2f nm | recorded window %.3f x %.3f mm | %zu x %zu samples",
            asset_->sourcePlateId.c_str(), view_.wavelengthMetres * 1e9,
            static_cast<double>(asset_->coupling.width()) * asset_->coupling.pitchXMetres() * 1e3,
            static_cast<double>(asset_->coupling.height()) * asset_->coupling.pitchYMetres() * 1e3,
            asset_->coupling.width(), asset_->coupling.height());
    const auto available = ImGui::GetContentRegionAvail();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 size{std::max(1.0F, available.x), std::max(1.0F, available.y - 60.0F)};
    canvasCentre = {origin.x + size.x * 0.5F, origin.y + size.y * 0.5F};
    ImGui::InvisibleButton("Showroom view", size,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    if (asset_ && ImGui::IsItemActive()) {
        const auto delta = ImGui::GetIO().MouseDelta;
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && (delta.x != 0 || delta.y != 0)) {
            yaw_ += delta.x * 0.0005F;
            pitch_ += delta.y * 0.0005F;
            changed = true;
        } else if (ImGui::IsMouseDown(ImGuiMouseButton_Right) && (delta.x != 0 || delta.y != 0)) {
            view_.eyePosition.x -= static_cast<double>(delta.x) * 1e-5;
            view_.eyePosition.y += static_cast<double>(delta.y) * 1e-5;
            changed = true;
        }
    }
    if (asset_ && ImGui::IsItemHovered() && ImGui::GetIO().MouseWheel != 0.0F) {
        view_.eyePosition.z = std::clamp(
            view_.eyePosition.z * std::exp(-0.08 * ImGui::GetIO().MouseWheel), 0.02, 1.0);
        changed = true;
    }
    if (changed)
        request();
    if (auto update = worker_.poll(); update && update->requestId == requestId_) {
        if (!update->error.empty()) {
            status_ = update->error;
            textureCurrent_ = false;
        } else {
            result_ = std::move(update->result);
            status_ = update->finalStage ? "Current / finite-window scalar result"
                                         : "Preview / refining pupil window";
            if (result_->boundaryPowerFraction > 0.01)
                status_ += " | boundary energy: enlarge recording window for convergence";
            upload();
        }
    }
    auto *draw = ImGui::GetWindowDrawList();
    const ImVec2 end{origin.x + size.x, origin.y + size.y},
        centre{origin.x + size.x / 2, origin.y + size.y / 2};
    draw->PushClipRect(origin, end, true);
    draw->AddRectFilled(origin, end, IM_COL32(0, 0, 0, 255));
    if (asset_ && math::isFinite(view_.eyePosition) && std::abs(view_.eyePosition.x) < 10.0 &&
        std::abs(view_.eyePosition.y) < 10.0 && view_.eyePosition.z > 0.0) {
        const double width =
            static_cast<double>(asset_->coupling.width()) * asset_->coupling.pitchXMetres();
        const double height =
            static_cast<double>(asset_->coupling.height()) * asset_->coupling.pitchYMetres();
        const double scale = 0.65 * std::min(size.x, size.y) * initial_.eyePosition.z /
                             (std::max(width, height) * view_.sensorDistanceMetres);
        if (textureCurrent_ && result_) {
            const auto &f = result_->sensorField;
            const float hw =
                static_cast<float>(0.5 * scale * static_cast<double>(f.width()) * f.pitchXMetres());
            const float hh = static_cast<float>(0.5 * scale * static_cast<double>(f.height()) *
                                                f.pitchYMetres());
            draw->AddImage(static_cast<ImTextureID>(texture_.handle()),
                           {centre.x - hw, centre.y - hh}, {centre.x + hw, centre.y + hh}, {1, 0},
                           {0, 1});
        }
        ImVec2 corners[4];
        const math::Vec3d local[4] = {{-width / 2, -height / 2, 0},
                                      {width / 2, -height / 2, 0},
                                      {width / 2, height / 2, 0},
                                      {-width / 2, height / 2, 0}};
        bool visible = true;
        for (int i = 0; i < 4; ++i) {
            const auto p = math::transformPointLocalToWorld(view_.platePose, local[i]);
            const double z = view_.eyePosition.z - p.z;
            visible &= z > 0.0;
            corners[i] = {
                centre.x + static_cast<float>(scale * view_.sensorDistanceMetres *
                                              (p.x - view_.eyePosition.x) / std::max(z, 1e-6)),
                centre.y - static_cast<float>(scale * view_.sensorDistanceMetres *
                                              (p.y - view_.eyePosition.y) / std::max(z, 1e-6))};
        }
        if (visible)
            draw->AddPolyline(corners, 4, IM_COL32(140, 155, 170, 255), ImDrawFlags_Closed, 2.0F);
    } else
        draw->AddText({origin.x + 20, origin.y + 20}, IM_COL32_WHITE,
                      "Record a reflection plate on the Bench, or open a saved .holo.json file.");
    draw->PopClipRect();
    ImGui::TextUnformatted("Drag: rotate plate | Right drag: move eye | Wheel: observation "
                           "distance | Outline: sampled plate window");
    ImGui::TextWrapped("%s", status_.c_str());
    ImGui::End();
    ImGui::PopStyleColor();
    return true;
}
} // namespace holobench::app
