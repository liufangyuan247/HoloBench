#pragma once

#include <cmath>
#include <string>
#include <vector>

#include "core/math/Vec3.hpp"

namespace holobench::optics::scene {

/**
 * @brief Solver-independent data model for a collimated beam light source.
 *
 * Physical Model & Conventions:
 * - SI units: positions in metres, beam radius in metres, wavelength in metres, power in Watts.
 * - Right-handed coordinate system with +Z nominal optical propagation.
 * - Emits a bundle of parallel rays with uniform circular beam radius.
 */
struct CollimatedSource final {
    std::string id = "collimated_source";
    math::Vec3d originMetres {0.0, 0.0, -0.15};
    math::Vec3d direction {0.0, 0.0, 1.0};
    double beamRadiusMetres = 0.01;
    double wavelengthMetres = 532e-9;
    double powerWatts = 1.0;

    bool operator==(const CollimatedSource&) const = default;
};

/**
 * @brief Solver-independent data model for an ideal planar mirror with a rectangular aperture.
 *
 * Physical Model & Conventions:
 * - SI units: plane position in metres, normal dimensionless (auto-normalized), aperture width/height in metres.
 * - Right-handed coordinate system.
 * - Normal vector defines the orientation of the mirror plane in 3D space.
 * - Width and height define the rectangular aperture boundaries centered at planePointMetres.
 */
struct PlanarMirror final {
    std::string id = "planar_mirror";
    math::Vec3d planePointMetres {0.0, 0.0, 0.0};
    math::Vec3d normal {0.0, 0.0, -1.0};
    double widthMetres = 0.05;
    double heightMetres = 0.05;

    bool operator==(const PlanarMirror&) const = default;
};

/**
 * @brief Solver-independent data model for a planar optical dielectric interface with a rectangular aperture.
 *
 * Physical Model & Conventions:
 * - SI units: plane position in metres, normal dimensionless, width/height in metres.
 * - Right-handed coordinate system.
 * - Refractive indices nIncident and nTransmitted are finite and strictly positive (> 0).
 * - Governed by Snell's law and total internal reflection (TIR).
 */
struct PlaneInterfaceComponent final {
    std::string id = "plane_interface";
    math::Vec3d planePointMetres {0.0, 0.0, 0.0};
    math::Vec3d normal {0.0, 0.0, -1.0};
    double widthMetres = 0.05;
    double heightMetres = 0.05;
    double nIncident = 1.0;
    double nTransmitted = 1.5;

    bool operator==(const PlaneInterfaceComponent&) const = default;
};

// Semantic validation functions (throw std::invalid_argument on failure)
void validateCollimatedSource(const CollimatedSource& source);
void validatePlanarMirror(const PlanarMirror& mirror);
void validatePlaneInterfaceComponent(const PlaneInterfaceComponent& interfaceComp);

[[nodiscard]] bool isCollimatedSourceValid(const CollimatedSource& source) noexcept;
[[nodiscard]] bool isPlanarMirrorValid(const PlanarMirror& mirror) noexcept;
[[nodiscard]] bool isPlaneInterfaceComponentValid(const PlaneInterfaceComponent& interfaceComp) noexcept;

// Helper to validate non-empty and unique component IDs
void validateUniqueComponentIds(const std::vector<std::string>& ids);

// Default factory functions
[[nodiscard]] CollimatedSource createDefaultCollimatedSource();
[[nodiscard]] PlanarMirror createDefaultPlanarMirror();
[[nodiscard]] PlaneInterfaceComponent createDefaultPlaneInterface();

} // namespace holobench::optics::scene
