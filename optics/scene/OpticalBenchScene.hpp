#pragma once

#include <cmath>
#include <string>

#include "core/math/Vec3.hpp"
#include "optics/ray/ThinLens.hpp"

namespace holobench::optics::scene {

enum class ImageNature {
    Real,
    Virtual,
    AtInfinity,
};

struct PointSource final {
    std::string id = "point_source";
    math::Vec3d positionMetres {0.0, 0.0, -0.15};
    double wavelengthMetres = 532e-9;
    double powerWatts = 1.0;

    bool operator==(const PointSource&) const = default;
};

struct ThinLensComponent final {
    std::string id = "thin_lens";
    double planeZMetres = 0.0;
    double centreXMetres = 0.0;
    double centreYMetres = 0.0;
    double focalLengthMetres = 0.05;
    double clearApertureRadiusMetres = 0.025;

    bool operator==(const ThinLensComponent&) const = default;

    [[nodiscard]] ray::IdealThinLens toIdealThinLens() const noexcept {
        return ray::IdealThinLens {
            .planeZMetres = planeZMetres,
            .centreXMetres = centreXMetres,
            .centreYMetres = centreYMetres,
            .focalLengthMetres = focalLengthMetres,
            .clearApertureRadiusMetres = clearApertureRadiusMetres,
        };
    }
};

struct CircularAperture final {
    std::string id = "aperture";
    double planeZMetres = 0.0;
    double centreXMetres = 0.0;
    double centreYMetres = 0.0;
    double radiusMetres = 0.025;

    bool operator==(const CircularAperture&) const = default;
};

struct ScreenComponent final {
    std::string id = "screen";
    double planeZMetres = 0.075;
    double centreXMetres = 0.0;
    double centreYMetres = 0.0;
    double widthMetres = 0.06;
    double heightMetres = 0.06;

    bool operator==(const ScreenComponent&) const = default;
};

struct OpticalBenchScene final {
    std::string name = "Default Optical Bench";
    PointSource source;
    ThinLensComponent lens;
    CircularAperture aperture;
    ScreenComponent screen;

    bool operator==(const OpticalBenchScene&) const = default;
};

struct ThinLensImagePrediction final {
    ImageNature nature = ImageNature::Real;
    double objectDistanceMetres = 0.0;    // u = z_lens - z_source
    double imageDistanceMetres = 0.0;     // v (+ for real, - for virtual, inf for infinity)
    double imagePlaneZMetres = 0.0;       // z_lens + v
    double transverseMagnification = 0.0; // m = -v / u
    math::Vec3d imagePositionMetres {};   // 3D predicted conjugate position
};

void validateScene(const OpticalBenchScene& scene);
[[nodiscard]] bool isSceneValid(const OpticalBenchScene& scene) noexcept;

[[nodiscard]] ThinLensImagePrediction predictThinLensImage(
    const OpticalBenchScene& scene,
    double focalEpsilonRel = 1e-7);

[[nodiscard]] OpticalBenchScene createDefaultRealImageScene();
[[nodiscard]] OpticalBenchScene createDefaultVirtualImageScene();
[[nodiscard]] OpticalBenchScene createDefaultInfinityScene();

} // namespace holobench::optics::scene
