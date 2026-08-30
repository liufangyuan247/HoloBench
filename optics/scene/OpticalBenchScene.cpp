#include "optics/scene/OpticalBenchScene.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace holobench::optics::scene {

void validateScene(const OpticalBenchScene& scene) {
    if (scene.name.empty()) {
        throw std::invalid_argument("scene name must not be empty");
    }

    // Point source validation
    if (scene.source.id.empty()) {
        throw std::invalid_argument("point source ID must not be empty");
    }
    if (!math::isFinite(scene.source.positionMetres)) {
        throw std::invalid_argument("point source position coordinates must be finite");
    }
    if (!std::isfinite(scene.source.wavelengthMetres) || scene.source.wavelengthMetres <= 0.0) {
        throw std::invalid_argument("point source wavelength must be finite and positive");
    }
    if (!std::isfinite(scene.source.powerWatts) || scene.source.powerWatts < 0.0) {
        throw std::invalid_argument("point source power must be finite and non-negative");
    }

    // Thin lens validation
    if (scene.lens.id.empty()) {
        throw std::invalid_argument("thin lens ID must not be empty");
    }
    if (!std::isfinite(scene.lens.planeZMetres)
        || !std::isfinite(scene.lens.centreXMetres)
        || !std::isfinite(scene.lens.centreYMetres)) {
        throw std::invalid_argument("thin lens position and centre coordinates must be finite");
    }
    if (!std::isfinite(scene.lens.focalLengthMetres) || scene.lens.focalLengthMetres == 0.0) {
        throw std::invalid_argument("thin lens focal length must be finite and non-zero");
    }
    if (!std::isfinite(scene.lens.clearApertureRadiusMetres) || scene.lens.clearApertureRadiusMetres <= 0.0) {
        throw std::invalid_argument("thin lens clear aperture radius must be finite and positive");
    }

    // Circular aperture validation
    if (scene.aperture.id.empty()) {
        throw std::invalid_argument("circular aperture ID must not be empty");
    }
    if (!std::isfinite(scene.aperture.planeZMetres)
        || !std::isfinite(scene.aperture.centreXMetres)
        || !std::isfinite(scene.aperture.centreYMetres)) {
        throw std::invalid_argument("circular aperture position and centre coordinates must be finite");
    }
    if (!std::isfinite(scene.aperture.radiusMetres) || scene.aperture.radiusMetres <= 0.0) {
        throw std::invalid_argument("circular aperture radius must be finite and positive");
    }

    // Screen validation
    if (scene.screen.id.empty()) {
        throw std::invalid_argument("screen ID must not be empty");
    }
    if (!std::isfinite(scene.screen.planeZMetres)
        || !std::isfinite(scene.screen.centreXMetres)
        || !std::isfinite(scene.screen.centreYMetres)) {
        throw std::invalid_argument("screen position and centre coordinates must be finite");
    }
    if (!std::isfinite(scene.screen.widthMetres) || scene.screen.widthMetres <= 0.0
        || !std::isfinite(scene.screen.heightMetres) || scene.screen.heightMetres <= 0.0) {
        throw std::invalid_argument("screen width and height must be finite and positive");
    }

    // Four component IDs must be pairwise unique
    if (scene.source.id == scene.lens.id
        || scene.source.id == scene.aperture.id
        || scene.source.id == scene.screen.id
        || scene.lens.id == scene.aperture.id
        || scene.lens.id == scene.screen.id
        || scene.aperture.id == scene.screen.id) {
        throw std::invalid_argument("optical bench component IDs must be pairwise unique");
    }

    // Nominal optical bench propagation geometry: source must precede lens along +Z
    if (scene.source.positionMetres.z >= scene.lens.planeZMetres) {
        throw std::invalid_argument("point source must be positioned before the lens along +Z optical axis (z_source < z_lens)");
    }
}

bool isSceneValid(const OpticalBenchScene& scene) noexcept {
    try {
        validateScene(scene);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

ThinLensImagePrediction predictThinLensImage(const OpticalBenchScene& scene, double focalEpsilonRel) {
    validateScene(scene);

    if (!std::isfinite(focalEpsilonRel) || focalEpsilonRel < 0.0) {
        throw std::invalid_argument("focalEpsilonRel must be finite and non-negative");
    }

    const double u = scene.lens.planeZMetres - scene.source.positionMetres.z;
    const double f = scene.lens.focalLengthMetres;
    const double delta = u - f;
    const double tolerance = focalEpsilonRel * std::abs(f);

    ThinLensImagePrediction prediction;
    prediction.objectDistanceMetres = u;

    if (std::abs(delta) <= tolerance) {
        prediction.nature = ImageNature::AtInfinity;
        prediction.imageDistanceMetres = std::numeric_limits<double>::infinity();
        prediction.imagePlaneZMetres = std::numeric_limits<double>::infinity();
        prediction.transverseMagnification = std::numeric_limits<double>::infinity();
        prediction.imagePositionMetres = {
            std::numeric_limits<double>::infinity(),
            std::numeric_limits<double>::infinity(),
            std::numeric_limits<double>::infinity(),
        };
        return prediction;
    }

    const double v = (u * f) / delta;
    const double m = -v / u;

    prediction.nature = (v > 0.0) ? ImageNature::Real : ImageNature::Virtual;
    prediction.imageDistanceMetres = v;
    prediction.imagePlaneZMetres = scene.lens.planeZMetres + v;
    prediction.transverseMagnification = m;

    const double dx = scene.source.positionMetres.x - scene.lens.centreXMetres;
    const double dy = scene.source.positionMetres.y - scene.lens.centreYMetres;
    prediction.imagePositionMetres = {
        scene.lens.centreXMetres + m * dx,
        scene.lens.centreYMetres + m * dy,
        scene.lens.planeZMetres + v,
    };

    return prediction;
}

OpticalBenchScene createDefaultRealImageScene() {
    OpticalBenchScene scene;
    scene.name = "Default Real Image Bench";
    scene.source = PointSource {
        .id = "source_point",
        .positionMetres = {0.0, 0.0, -0.15},
        .wavelengthMetres = 532e-9,
        .powerWatts = 1.0,
    };
    scene.lens = ThinLensComponent {
        .id = "lens_thin",
        .planeZMetres = 0.0,
        .centreXMetres = 0.0,
        .centreYMetres = 0.0,
        .focalLengthMetres = 0.05,
        .clearApertureRadiusMetres = 0.025,
    };
    scene.aperture = CircularAperture {
        .id = "aperture_stop",
        .planeZMetres = 0.0,
        .centreXMetres = 0.0,
        .centreYMetres = 0.0,
        .radiusMetres = 0.025,
    };
    scene.screen = ScreenComponent {
        .id = "screen_target",
        .planeZMetres = 0.075,
        .centreXMetres = 0.0,
        .centreYMetres = 0.0,
        .widthMetres = 0.06,
        .heightMetres = 0.06,
    };
    return scene;
}

OpticalBenchScene createDefaultVirtualImageScene() {
    OpticalBenchScene scene = createDefaultRealImageScene();
    scene.name = "Default Virtual Image Bench";
    scene.source.positionMetres = {0.0, 0.0, -0.03};
    scene.screen.planeZMetres = 0.15;
    return scene;
}

OpticalBenchScene createDefaultInfinityScene() {
    OpticalBenchScene scene = createDefaultRealImageScene();
    scene.name = "Default Collimated Bench";
    scene.source.positionMetres = {0.0, 0.0, -0.05};
    scene.screen.planeZMetres = 0.15;
    return scene;
}

} // namespace holobench::optics::scene
