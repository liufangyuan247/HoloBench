#include "app/LensProfileEditor.hpp"

#include <algorithm>
#include <cmath>
#include <imgui.h>
#include <limits>

namespace holobench::app {
bool drawLensProfileEditor(optics::ray::SequentialLensPrescription &p, std::size_t &selected,
                           std::string &diagnostic) {
    bool changed = false;
    const auto edit = [&](auto operation) {
        try {
            operation();
            diagnostic.clear();
            changed = true;
        } catch (const std::exception &e) {
            diagnostic = e.what();
        }
    };
    ImGui::TextUnformatted("Meridional lens design | horizontal Z, vertical Y | dimensions in mm");
    if (ImGui::Button("Append N-BK7 lens / 10 mm air gap"))
        edit([&] {
            appendProfileLens(p);
            selected = p.surfaces.size() - 2U;
        });
    ImGui::SameLine();
    ImGui::BeginDisabled(p.surfaces.size() < 4U);
    if (ImGui::Button("Remove last lens"))
        edit([&] {
            auto candidate = p;
            candidate.surfaces.resize(candidate.surfaces.size() - 2U);
            validateLensProfileAssembly(candidate);
            p = std::move(candidate);
            selected = 0;
        });
    ImGui::EndDisabled();
    if (p.surfaces.empty())
        return changed;
    selected = std::min(selected, p.surfaces.size() - 1U);
    if (ImGui::BeginCombo("Surface", p.surfaces[selected].id.c_str())) {
        for (std::size_t i = 0; i < p.surfaces.size(); ++i)
            if (ImGui::Selectable(p.surfaces[i].id.c_str(), i == selected))
                selected = i;
        ImGui::EndCombo();
    }
    auto &current = p.surfaces[selected];
    try {
        double sag = optics::ray::evaluateSurfaceSag(current.geometry,
                                                     current.geometry.clearSemiDiameterMetres)
                         .sagMetres *
                     1e3;
        if (ImGui::InputDouble("Edge sag (mm)", &sag, 0.01, 0.1, "%.6f"))
            edit([&] { setLensSurfaceEdgeSag(p, selected, sag * 1e-3); });
        if (selected > 0U) {
            double spacing = (p.surfaces[selected].localToWorld.translationMetres.z -
                              p.surfaces[selected - 1U].localToWorld.translationMetres.z) *
                             1e3;
            if (ImGui::InputDouble("Thickness / preceding air gap (mm)", &spacing, 0.1, 1, "%.6f"))
                edit([&] { setLensSurfaceSpacing(p, selected, spacing * 1e-3); });
        }
        // Retrieve after a transactional edit: the previous reference may have moved.
        auto candidate = p;
        double diameter = candidate.surfaces[selected].geometry.clearSemiDiameterMetres * 2e3;
        if (ImGui::InputDouble("Clear diameter (mm)", &diameter, 0.1, 1, "%.4f"))
            edit([&] {
                candidate.surfaces[selected].geometry.clearSemiDiameterMetres = diameter * 0.5e-3;
                validateLensProfileAssembly(candidate);
                p = std::move(candidate);
            });
        ImGui::TextWrapped("%s -> %s. Conic, asphere coefficients and glass materials are editable "
                           "in Prescription Editor. Save under a new ID and reload to bind this "
                           "group to a Bench Real Lens Assembly.",
                           p.surfaces[selected].materialBeforeId.c_str(),
                           p.surfaces[selected].materialAfterId.c_str());
        std::vector<std::vector<math::Vec3d>> profiles;
        double minZ = std::numeric_limits<double>::infinity(), maxZ = -minZ, maxY = 0.0;
        for (const auto &s : p.surfaces) {
            profiles.push_back(lensSurfaceProfile(s));
            for (const auto &point : profiles.back()) {
                minZ = std::min(minZ, point.z);
                maxZ = std::max(maxZ, point.z);
                maxY = std::max(maxY, std::abs(point.y));
            }
        }
        const auto origin = ImGui::GetCursorScreenPos();
        const ImVec2 size{std::max(200.0F, ImGui::GetContentRegionAvail().x), 300.0F};
        const double scale = std::min((size.x - 60.0) / std::max(0.01, maxZ - minZ),
                                      (size.y - 50.0) / std::max(0.01, 2 * maxY));
        const auto project = [&](math::Vec3d point) {
            return ImVec2{origin.x + size.x / 2 +
                              static_cast<float>((point.z - (minZ + maxZ) / 2) * scale),
                          origin.y + size.y / 2 - static_cast<float>(point.y * scale)};
        };
        ImGui::InvisibleButton("Lens section canvas", size);
        auto *draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(origin, {origin.x + size.x, origin.y + size.y},
                            IM_COL32(9, 14, 20, 255));
        draw->AddLine({origin.x, origin.y + size.y / 2}, {origin.x + size.x, origin.y + size.y / 2},
                      IM_COL32(90, 90, 90, 255));
        const auto mouse = ImGui::GetIO().MousePos;
        double nearest = 100.0;
        std::size_t nearestSurface = selected;
        for (std::size_t i = 0; i < profiles.size(); ++i) {
            const auto color =
                i == selected ? IM_COL32(255, 195, 80, 255) : IM_COL32(85, 200, 225, 255);
            for (std::size_t j = 1; j < profiles[i].size(); ++j) {
                const auto a = project(profiles[i][j - 1U]), b = project(profiles[i][j]);
                draw->AddLine(a, b, color, 2.0F);
                const double d = std::hypot(mouse.x - b.x, mouse.y - b.y);
                if (d < nearest) {
                    nearest = d;
                    nearestSurface = i;
                }
            }
            if (i + 1U < profiles.size() && p.surfaces[i].materialAfterId != "air" &&
                p.surfaces[i].materialAfterId != "vacuum") {
                draw->AddLine(project(profiles[i].front()), project(profiles[i + 1U].front()),
                              color);
                draw->AddLine(project(profiles[i].back()), project(profiles[i + 1U].back()), color);
            }
        }
        if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
            nearest < 14.0)
            selected = nearestSurface;
        const auto handle = project(profiles[selected].back());
        draw->AddCircleFilled(handle, 5.0F, IM_COL32(255, 195, 80, 255));
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            // Shift-drag edits preceding thickness; ordinary drag edits edge sag.
            const double delta = static_cast<double>(ImGui::GetIO().MouseDelta.x) / scale;
            if (delta != 0.0)
                edit([&] {
                    if (ImGui::GetIO().KeyShift && selected > 0U)
                        setLensSurfaceSpacing(
                            p, selected,
                            p.surfaces[selected].localToWorld.translationMetres.z -
                                p.surfaces[selected - 1U].localToWorld.translationMetres.z + delta);
                    else
                        setLensSurfaceEdgeSag(
                            p, selected,
                            optics::ray::evaluateSurfaceSag(
                                p.surfaces[selected].geometry,
                                p.surfaces[selected].geometry.clearSemiDiameterMetres)
                                    .sagMetres +
                                delta);
                });
        }
    } catch (const std::exception &e) {
        diagnostic = e.what();
    }
    ImGui::TextUnformatted("Click a curve; drag horizontally: edge sag | Shift-drag: preceding "
                           "spacing and downstream group");
    if (!diagnostic.empty())
        ImGui::TextWrapped("%s", diagnostic.c_str());
    return changed;
}
} // namespace holobench::app
