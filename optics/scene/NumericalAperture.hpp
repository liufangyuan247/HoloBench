#pragma once

#include <cmath>
#include <limits>
#include <numbers>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>

#include "core/math/Vec3.hpp"
#include "optics/scene/OpticalBenchScene.hpp"

namespace holobench::optics::scene {

enum class LimitingStopKind {
    Lens,
    Aperture,
};

[[nodiscard]] inline std::string toString(LimitingStopKind kind) {
    switch (kind) {
    case LimitingStopKind::Lens:
        return "Thin Lens";
    case LimitingStopKind::Aperture:
        return "Independent Aperture";
    }
    return "Unknown";
}

inline std::ostream& operator<<(std::ostream& os, LimitingStopKind kind) {
    return os << toString(kind);
}

struct ObjectSideNumericalApertureResult final {
    LimitingStopKind limitingStop = LimitingStopKind::Lens;
    std::string limitingStopId = "thin_lens";
    double axialDistanceMetres = 0.0;
    double halfAngleRadians = 0.0;
    double numericalAperture = 0.0;
    math::Vec3d rimCenterMetres {};
    double rimRadiusMetres = 0.0;
    bool downstreamStopNotModeled = false;
    bool approximate = false;
    std::string warningMessage {};

    bool operator==(const ObjectSideNumericalApertureResult&) const = default;
};

/**
 * @brief Computes the object-side Numerical Aperture (NA) for a point source in an optical bench scene.
 *
 * Physical definition:
 *   NA = n * sin(theta_max)
 * where n is the refractive index of the object medium (n > 0), and theta_max is the marginal acceptance
 * half-angle subtended by the limiting entrance pupil / aperture stop as seen from the point source.
 *
 * Limiting Stop Selection & Physical Assumptions:
 *   1. Pre-lens / upstream stop (z_source < z_aperture <= z_lens):
 *      Both the standalone circular aperture and the thin lens clear aperture lie in object space.
 *      The element with the strictly smaller subtended acceptance half-angle theta = atan2(radius, delta_z)
 *      acts as the physical entrance pupil.
 *   2. Stop at or behind source (z_aperture <= z_source):
 *      The standalone aperture cannot clip forward (+Z) rays emitted toward the lens; the lens clear
 *      aperture is the limiting stop.
 *   3. Downstream stop behind lens (z_aperture > z_lens):
 *      In full physical optics, an aperture stop behind the lens forms an entrance pupil via backward imaging
 *      through the lens into object space. In this M1 geometric model, backward pupil imaging is not modeled.
 *      The calculation uses the lens clear aperture as an approximation and flags `downstreamStopNotModeled = true`,
 *      `approximate = true`, and populates `warningMessage`.
 *   4. Off-axis source / decentered stop:
 *      Axial NA is strictly defined for on-axis object points. When the point source or limiting stop is
 *      transversely decentered (rho > 0), the subtended ray cone is asymmetric. The nominal acceptance
 *      half-angle is returned, `approximate = true` is flagged, and an informative `warningMessage` is set.
 *
 * @param scene Validated optical bench scene.
 * @param n Refractive index of the object space medium (must be finite and > 0, default 1.0).
 * @return ObjectSideNumericalApertureResult containing the limiting stop, NA, half angle, and warning flags.
 * @throws std::invalid_argument if scene validation fails or n is non-finite / non-positive.
 * @throws std::runtime_error if numerical calculations produce non-finite outputs.
 */
[[nodiscard]] inline ObjectSideNumericalApertureResult computeObjectSideNumericalAperture(
    const OpticalBenchScene& scene,
    double n = 1.0) {
    validateScene(scene);

    if (!std::isfinite(n) || n <= 0.0) {
        throw std::invalid_argument("refractive index n must be finite and positive");
    }

    const double xs = scene.source.positionMetres.x;
    const double ys = scene.source.positionMetres.y;
    const double zs = scene.source.positionMetres.z;

    const double xl = scene.lens.centreXMetres;
    const double yl = scene.lens.centreYMetres;
    const double zl = scene.lens.planeZMetres;

    const double za = scene.aperture.planeZMetres;

    const double dl = zl - zs;
    if (!std::isfinite(dl) || dl <= 0.0) {
        throw std::invalid_argument("lens axial distance must be finite and positive");
    }

    const double rl = scene.lens.clearApertureRadiusMetres;
    if (!std::isfinite(rl) || rl <= 0.0) {
        throw std::invalid_argument("lens clear aperture radius must be finite and positive");
    }

    const double thetaLens = std::atan2(rl, dl);
    if (!std::isfinite(thetaLens) || thetaLens <= 0.0) {
        throw std::invalid_argument("lens subtended half angle must be finite and positive");
    }

    const double rhoLens = std::hypot(xl - xs, yl - ys);
    const bool isLensOffAxis = rhoLens > 1e-12;

    ObjectSideNumericalApertureResult result;

    if (za > zl) {
        // Downstream aperture located behind the lens (image space).
        // Backward pupil imaging is not modeled in M1. We use lens clear aperture as an approximation.
        result.limitingStop = LimitingStopKind::Lens;
        result.limitingStopId = scene.lens.id;
        result.axialDistanceMetres = dl;
        result.halfAngleRadians = thetaLens;
        result.numericalAperture = n * (rl / std::hypot(dl, rl));
        result.rimCenterMetres = math::Vec3d {xl, yl, zl};
        result.rimRadiusMetres = rl;
        result.downstreamStopNotModeled = true;
        result.approximate = true;
        if (isLensOffAxis) {
            result.warningMessage = "Limiting stop is off-axis (asymmetric cone), and aperture is behind lens (backward pupil not modeled); NA is approximate.";
        } else {
            result.warningMessage = "Aperture is located behind the lens (image space). Backward-imaged entrance pupil is not modeled in object-side NA; using lens clear aperture as an approximation.";
        }
    } else if (za > zs) {
        // Standalone aperture is located in front of or coplanar with the lens (zs < za <= zl).
        const double da = za - zs;
        if (!std::isfinite(da) || da <= 0.0) {
            throw std::invalid_argument("aperture axial distance must be finite and positive");
        }

        const double ra = scene.aperture.radiusMetres;
        if (!std::isfinite(ra) || ra <= 0.0) {
            throw std::invalid_argument("aperture radius must be finite and positive");
        }

        const double thetaAperture = std::atan2(ra, da);
        if (!std::isfinite(thetaAperture) || thetaAperture <= 0.0) {
            throw std::invalid_argument("aperture subtended half angle must be finite and positive");
        }

        const double xa = scene.aperture.centreXMetres;
        const double ya = scene.aperture.centreYMetres;
        const double rhoAperture = std::hypot(xa - xs, ya - ys);
        const bool isApertureOffAxis = rhoAperture > 1e-12;

        // Compare acceptance half-angles: standalone aperture limits if strictly smaller than lens.
        if (thetaAperture < thetaLens) {
            result.limitingStop = LimitingStopKind::Aperture;
            result.limitingStopId = scene.aperture.id;
            result.axialDistanceMetres = da;
            result.halfAngleRadians = thetaAperture;
            result.numericalAperture = n * (ra / std::hypot(da, ra));
            result.rimCenterMetres = math::Vec3d {xa, ya, za};
            result.rimRadiusMetres = ra;
            result.downstreamStopNotModeled = false;
            if (isApertureOffAxis) {
                result.approximate = true;
                result.warningMessage = "Point source is off-axis relative to limiting aperture stop. The acceptance cone is asymmetric; NA is approximate.";
            } else {
                result.approximate = false;
                result.warningMessage.clear();
            }
        } else {
            result.limitingStop = LimitingStopKind::Lens;
            result.limitingStopId = scene.lens.id;
            result.axialDistanceMetres = dl;
            result.halfAngleRadians = thetaLens;
            result.numericalAperture = n * (rl / std::hypot(dl, rl));
            result.rimCenterMetres = math::Vec3d {xl, yl, zl};
            result.rimRadiusMetres = rl;
            result.downstreamStopNotModeled = false;
            if (isLensOffAxis) {
                result.approximate = true;
                result.warningMessage = "Point source is off-axis relative to limiting lens clear aperture. The acceptance cone is asymmetric; NA is approximate.";
            } else {
                result.approximate = false;
                result.warningMessage.clear();
            }
        }
    } else {
        // Standalone aperture is located at or behind the point source (za <= zs), so it does not limit forward cone.
        result.limitingStop = LimitingStopKind::Lens;
        result.limitingStopId = scene.lens.id;
        result.axialDistanceMetres = dl;
        result.halfAngleRadians = thetaLens;
        result.numericalAperture = n * (rl / std::hypot(dl, rl));
        result.rimCenterMetres = math::Vec3d {xl, yl, zl};
        result.rimRadiusMetres = rl;
        result.downstreamStopNotModeled = false;
        if (isLensOffAxis) {
            result.approximate = true;
            result.warningMessage = "Point source is off-axis relative to limiting lens clear aperture. The acceptance cone is asymmetric; NA is approximate.";
        } else {
            result.approximate = false;
            result.warningMessage.clear();
        }
    }

    if (!std::isfinite(result.axialDistanceMetres)
        || result.axialDistanceMetres <= 0.0
        || !std::isfinite(result.halfAngleRadians)
        || result.halfAngleRadians <= 0.0
        || !std::isfinite(result.numericalAperture)
        || result.numericalAperture <= 0.0
        || !math::isFinite(result.rimCenterMetres)
        || !std::isfinite(result.rimRadiusMetres)
        || result.rimRadiusMetres <= 0.0) {
        throw std::runtime_error("numerical aperture calculation produced non-finite or non-positive results");
    }

    return result;
}

} // namespace holobench::optics::scene
