#include "optics/scene/GeometricComponents.hpp"

#include <cmath>
#include <stdexcept>
#include <unordered_set>

namespace holobench::optics::scene {

void validateCollimatedSource(const CollimatedSource& source) {
    if (source.id.empty()) {
        throw std::invalid_argument("collimated source ID must not be empty");
    }
    if (!math::isFinite(source.originMetres)) {
        throw std::invalid_argument("collimated source origin coordinates must be finite");
    }
    if (!math::isFinite(source.direction) || math::lengthSquared(source.direction) <= 0.0) {
        throw std::invalid_argument("collimated source direction must be finite and non-zero");
    }
    if (!std::isfinite(source.beamRadiusMetres) || source.beamRadiusMetres <= 0.0) {
        throw std::invalid_argument("collimated source beam radius must be finite and positive");
    }
    if (!std::isfinite(source.wavelengthMetres) || source.wavelengthMetres <= 0.0) {
        throw std::invalid_argument("collimated source wavelength must be finite and positive");
    }
    if (!std::isfinite(source.powerWatts) || source.powerWatts < 0.0) {
        throw std::invalid_argument("collimated source power must be finite and non-negative");
    }
}

void validatePlanarMirror(const PlanarMirror& mirror) {
    if (mirror.id.empty()) {
        throw std::invalid_argument("planar mirror ID must not be empty");
    }
    if (!math::isFinite(mirror.planePointMetres)) {
        throw std::invalid_argument("planar mirror plane point coordinates must be finite");
    }
    if (!math::isFinite(mirror.normal) || math::lengthSquared(mirror.normal) <= 0.0) {
        throw std::invalid_argument("planar mirror normal must be finite and non-zero");
    }
    if (!std::isfinite(mirror.widthMetres) || mirror.widthMetres <= 0.0) {
        throw std::invalid_argument("planar mirror width must be finite and positive");
    }
    if (!std::isfinite(mirror.heightMetres) || mirror.heightMetres <= 0.0) {
        throw std::invalid_argument("planar mirror height must be finite and positive");
    }
}

void validatePlaneInterfaceComponent(const PlaneInterfaceComponent& interfaceComp) {
    if (interfaceComp.id.empty()) {
        throw std::invalid_argument("plane interface component ID must not be empty");
    }
    if (!math::isFinite(interfaceComp.planePointMetres)) {
        throw std::invalid_argument("plane interface component plane point coordinates must be finite");
    }
    if (!math::isFinite(interfaceComp.normal) || math::lengthSquared(interfaceComp.normal) <= 0.0) {
        throw std::invalid_argument("plane interface component normal must be finite and non-zero");
    }
    if (!std::isfinite(interfaceComp.widthMetres) || interfaceComp.widthMetres <= 0.0) {
        throw std::invalid_argument("plane interface component width must be finite and positive");
    }
    if (!std::isfinite(interfaceComp.heightMetres) || interfaceComp.heightMetres <= 0.0) {
        throw std::invalid_argument("plane interface component height must be finite and positive");
    }
    if (!std::isfinite(interfaceComp.nIncident) || interfaceComp.nIncident <= 0.0) {
        throw std::invalid_argument("plane interface incident refractive index must be finite and positive");
    }
    if (!std::isfinite(interfaceComp.nTransmitted) || interfaceComp.nTransmitted <= 0.0) {
        throw std::invalid_argument("plane interface transmitted refractive index must be finite and positive");
    }
}

bool isCollimatedSourceValid(const CollimatedSource& source) noexcept {
    try {
        validateCollimatedSource(source);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool isPlanarMirrorValid(const PlanarMirror& mirror) noexcept {
    try {
        validatePlanarMirror(mirror);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool isPlaneInterfaceComponentValid(const PlaneInterfaceComponent& interfaceComp) noexcept {
    try {
        validatePlaneInterfaceComponent(interfaceComp);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

void validateUniqueComponentIds(const std::vector<std::string>& ids) {
    std::unordered_set<std::string> seen;
    seen.reserve(ids.size());
    for (const auto& id : ids) {
        if (id.empty()) {
            throw std::invalid_argument("component ID must not be empty");
        }
        if (!seen.insert(id).second) {
            throw std::invalid_argument("duplicate component ID detected: " + id);
        }
    }
}

CollimatedSource createDefaultCollimatedSource() {
    return CollimatedSource {
        .id = "collimated_source",
        .originMetres = {0.0, 0.0, -0.15},
        .direction = {0.0, 0.0, 1.0},
        .beamRadiusMetres = 0.01,
        .wavelengthMetres = 532e-9,
        .powerWatts = 1.0,
    };
}

PlanarMirror createDefaultPlanarMirror() {
    return PlanarMirror {
        .id = "planar_mirror",
        .planePointMetres = {0.0, 0.0, 0.0},
        .normal = {0.0, 0.0, -1.0},
        .widthMetres = 0.05,
        .heightMetres = 0.05,
    };
}

PlaneInterfaceComponent createDefaultPlaneInterface() {
    return PlaneInterfaceComponent {
        .id = "plane_interface",
        .planePointMetres = {0.0, 0.0, 0.0},
        .normal = {0.0, 0.0, -1.0},
        .widthMetres = 0.05,
        .heightMetres = 0.05,
        .nIncident = 1.0,
        .nTransmitted = 1.5,
    };
}

} // namespace holobench::optics::scene
