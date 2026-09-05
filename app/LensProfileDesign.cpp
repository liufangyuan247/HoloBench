#include "app/LensProfileDesign.hpp"

#include "optics/ray/LensPrescriptionCatalog.hpp"
#include "optics/ray/SurfaceDesign.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace holobench::app {
namespace ray = optics::ray;
std::vector<math::Vec3d> lensSurfaceProfile(const ray::PrescriptionSurface &surface,
                                            std::size_t segments) {
    if (segments < 8U || segments > 512U)
        throw std::invalid_argument("Profile requires 8 to 512 segments");
    ray::validateRotationalSurface(surface.geometry);
    math::validateRigidTransform(surface.localToWorld);
    std::vector<math::Vec3d> points;
    for (std::size_t i = 0; i <= segments; ++i) {
        const double r = surface.geometry.clearSemiDiameterMetres *
                         (2.0 * static_cast<double>(i) / static_cast<double>(segments) - 1.0);
        const double z = ray::evaluateSurfaceSag(surface.geometry, std::abs(r)).sagMetres;
        points.push_back(math::transformPointLocalToWorld(surface.localToWorld, {0, r, z}));
    }
    return points;
}
void validateLensProfileAssembly(const ray::SequentialLensPrescription &p) {
    ray::validateSequentialLensPrescription(p);
    if (p.surfaces.size() > 128U)
        throw std::invalid_argument("Profile editor supports at most 128 surfaces");
    for (std::size_t i = 0; i < p.surfaces.size(); ++i) {
        const auto &s = p.surfaces[i];
        auto centred = s.localToWorld;
        centred.translationMetres = {};
        if (centred != math::RigidTransform3d{} || s.localToWorld.translationMetres.x != 0.0 ||
            s.localToWorld.translationMetres.y != 0.0)
            throw std::invalid_argument("2D assembly editing requires coaxial surfaces; use the "
                                        "prescription editor for decentre and tilt");
        if (i == 0U)
            continue;
        const auto &before = p.surfaces[i - 1U];
        const double radius =
            std::min(before.geometry.clearSemiDiameterMetres, s.geometry.clearSemiDiameterMetres);
        for (std::size_t sample = 0; sample <= 256U; ++sample) {
            const double r = radius * static_cast<double>(sample) / 256.0;
            const double left = before.localToWorld.translationMetres.z +
                                ray::evaluateSurfaceSag(before.geometry, r).sagMetres;
            const double right = s.localToWorld.translationMetres.z +
                                 ray::evaluateSurfaceSag(s.geometry, r).sagMetres;
            if (right - left < 1e-6)
                throw std::invalid_argument("Surfaces intersect or spacing is below 1 um in the "
                                            "sampled meridional aperture");
        }
    }
}
void setLensSurfaceEdgeSag(ray::SequentialLensPrescription &p, std::size_t index, double sag) {
    auto candidate = p;
    auto &g = candidate.surfaces.at(index).geometry;
    g = ray::surfaceWithEdgeSag(g, sag);
    validateLensProfileAssembly(candidate);
    p = std::move(candidate);
}
void setLensSurfaceSpacing(ray::SequentialLensPrescription &p, std::size_t index, double spacing) {
    if (index == 0U || index >= p.surfaces.size() || !std::isfinite(spacing) || spacing < 1e-6)
        throw std::invalid_argument("Select a surface after the first and a positive spacing");
    auto candidate = p;
    const double delta = spacing - (p.surfaces[index].localToWorld.translationMetres.z -
                                    p.surfaces[index - 1U].localToWorld.translationMetres.z);
    for (std::size_t i = index; i < candidate.surfaces.size(); ++i)
        candidate.surfaces[i].localToWorld.translationMetres.z += delta;
    validateLensProfileAssembly(candidate);
    p = std::move(candidate);
}
void appendProfileLens(ray::SequentialLensPrescription &p) {
    auto candidate = p;
    auto lens = ray::makeDefaultNBk7BiconvexPrescription();
    const double offset = candidate.surfaces.empty()
                              ? 0.0
                              : candidate.surfaces.back().localToWorld.translationMetres.z + 0.01;
    if (!candidate.surfaces.empty() &&
        candidate.surfaces.back().materialAfterId != lens.surfaces.front().materialBeforeId)
        throw std::invalid_argument("End the existing group in air before appending a lens");
    for (const auto &material : lens.materials) {
        const auto found = std::find_if(candidate.materials.begin(), candidate.materials.end(),
                                        [&](const auto &m) { return m.id == material.id; });
        if (found == candidate.materials.end())
            candidate.materials.push_back(material);
        else if (*found != material)
            throw std::invalid_argument("Existing glass ID differs from the appended lens glass");
    }
    for (auto &surface : lens.surfaces) {
        surface.id = "profile_surface_" + std::to_string(candidate.surfaces.size() + 1U);
        surface.localToWorld.translationMetres.z += offset;
        candidate.surfaces.push_back(std::move(surface));
    }
    validateLensProfileAssembly(candidate);
    p = std::move(candidate);
}
} // namespace holobench::app
