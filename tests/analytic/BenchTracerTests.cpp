#include <doctest/doctest.h>

#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

#include "optics/ray/BenchTracer.hpp"
#include "optics/scene/OpticalBenchScene.hpp"

namespace {

using namespace holobench;
using namespace holobench::optics;

} // namespace

TEST_CASE("default optical bench scene is valid and predicts real image") {
    const auto scene = scene::createDefaultRealImageScene();
    CHECK(scene::isSceneValid(scene));

    const auto prediction = scene::predictThinLensImage(scene);
    CHECK(prediction.nature == scene::ImageNature::Real);
    CHECK(prediction.objectDistanceMetres == doctest::Approx(0.15));
    CHECK(prediction.imageDistanceMetres == doctest::Approx(0.075));
    CHECK(prediction.imagePlaneZMetres == doctest::Approx(0.075));
    CHECK(prediction.transverseMagnification == doctest::Approx(-0.5));
    CHECK(prediction.imagePositionMetres.x == doctest::Approx(0.0));
    CHECK(prediction.imagePositionMetres.y == doctest::Approx(0.0));
    CHECK(prediction.imagePositionMetres.z == doctest::Approx(0.075));
}

TEST_CASE("real image tracing produces converging rays at analytic focus") {
    auto scene = scene::createDefaultRealImageScene();
    scene.source.positionMetres = {0.010, -0.006, -0.15}; // off-axis point source
    scene.screen.planeZMetres = 0.075;                    // placed at image plane

    const auto prediction = scene::predictThinLensImage(scene);
    REQUIRE(prediction.nature == scene::ImageNature::Real);
    const double expectedX = -0.5 * 0.010;
    const double expectedY = -0.5 * (-0.006);
    CHECK(prediction.imagePositionMetres.x == doctest::Approx(expectedX).epsilon(1e-12));
    CHECK(prediction.imagePositionMetres.y == doctest::Approx(expectedY).epsilon(1e-12));

    ray::BenchTracerOptions options;
    options.rayCount = 40;
    options.pattern = ray::RaySamplingPattern::FibonacciDisk;

    std::vector<ray::RaySegment> segments;
    ray::traceBench(scene, options, segments);

    REQUIRE_FALSE(segments.empty());

    std::size_t incidentCount = 0;
    std::size_t transmittedCount = 0;
    for (const auto& seg : segments) {
        if (seg.kind == ray::RaySegmentKind::Incident) {
            ++incidentCount;
            CHECK(seg.startMetres.z == doctest::Approx(-0.15));
            CHECK(seg.endMetres.z == doctest::Approx(0.0));
        } else if (seg.kind == ray::RaySegmentKind::Transmitted) {
            ++transmittedCount;
            // The segment end should land on the screen at (expectedX, expectedY, 0.075)
            CHECK(seg.startMetres.z == doctest::Approx(0.0));
            CHECK(seg.endMetres.z == doctest::Approx(0.075).epsilon(1e-12));
            CHECK(seg.endMetres.x == doctest::Approx(expectedX).epsilon(1e-10));
            CHECK(seg.endMetres.y == doctest::Approx(expectedY).epsilon(1e-10));
        }
    }
    CHECK(incidentCount == options.rayCount);
    CHECK(transmittedCount == options.rayCount);
}

TEST_CASE("virtual image prediction and backward ray extension segments") {
    auto scene = scene::createDefaultVirtualImageScene();
    // u = 0.03 m, f = 0.05 m => 1/v = 1/0.05 - 1/0.03 = 20 - 33.3333 = -13.3333 => v = -0.075 m
    // m = -(-0.075) / 0.03 = +2.5
    scene.source.positionMetres = {0.004, -0.002, -0.03};
    scene.screen.planeZMetres = 0.10;

    const auto prediction = scene::predictThinLensImage(scene);
    REQUIRE(prediction.nature == scene::ImageNature::Virtual);
    CHECK(prediction.objectDistanceMetres == doctest::Approx(0.03));
    CHECK(prediction.imageDistanceMetres == doctest::Approx(-0.075));
    CHECK(prediction.imagePlaneZMetres == doctest::Approx(-0.075));
    CHECK(prediction.transverseMagnification == doctest::Approx(2.5));

    const double expectedVirtX = 2.5 * 0.004;
    const double expectedVirtY = 2.5 * (-0.002);
    CHECK(prediction.imagePositionMetres.x == doctest::Approx(expectedVirtX).epsilon(1e-12));
    CHECK(prediction.imagePositionMetres.y == doctest::Approx(expectedVirtY).epsilon(1e-12));
    CHECK(prediction.imagePositionMetres.z == doctest::Approx(-0.075).epsilon(1e-12));

    ray::BenchTracerOptions options;
    options.rayCount = 32;
    options.pattern = ray::RaySamplingPattern::CrossFans;
    options.includeVirtualExtensions = true;
    options.virtualExtensionDistanceMetres = 1.0;

    const auto segments = ray::traceBench(scene, options);

    std::size_t incidentCount = 0;
    std::size_t transmittedCount = 0;
    std::size_t virtualCount = 0;

    for (const auto& seg : segments) {
        if (seg.kind == ray::RaySegmentKind::Incident) {
            ++incidentCount;
            CHECK(seg.startMetres.z == doctest::Approx(-0.03));
            CHECK(seg.endMetres.z == doctest::Approx(0.0));
        } else if (seg.kind == ray::RaySegmentKind::Transmitted) {
            ++transmittedCount;
            CHECK(seg.startMetres.z == doctest::Approx(0.0));
            CHECK(seg.endMetres.z == doctest::Approx(0.10));
        } else if (seg.kind == ray::RaySegmentKind::VirtualExtension) {
            ++virtualCount;
            CHECK(seg.startMetres.z == doctest::Approx(0.0));
            CHECK(seg.endMetres.z == doctest::Approx(-0.075).epsilon(1e-12));
            CHECK(seg.endMetres.x == doctest::Approx(expectedVirtX).epsilon(1e-10));
            CHECK(seg.endMetres.y == doctest::Approx(expectedVirtY).epsilon(1e-10));
        }
    }

    CHECK(incidentCount == options.rayCount);
    CHECK(transmittedCount == options.rayCount);
    CHECK(virtualCount == options.rayCount);
}

TEST_CASE("virtual extension distance cap respects virtualExtensionDistanceMetres") {
    auto scene = scene::createDefaultVirtualImageScene();
    scene.source.positionMetres = {0.0, 0.0, -0.03}; // conjugate at v = -0.075 m (distance to lens is 0.075m on-axis)
    scene.screen.planeZMetres = 0.10;

    // Test 1: virtualExtensionDistanceMetres = 0.03 m (< 0.075 m) caps backward extension length
    {
        ray::BenchTracerOptions options;
        options.rayCount = 10;
        options.pattern = ray::RaySamplingPattern::MeridionalFan;
        options.includeVirtualExtensions = true;
        options.virtualExtensionDistanceMetres = 0.03;

        const auto segments = ray::traceBench(scene, options);
        std::size_t virtualCount = 0;
        for (const auto& seg : segments) {
            if (seg.kind == ray::RaySegmentKind::VirtualExtension) {
                ++virtualCount;
                CHECK(seg.startMetres.z == doctest::Approx(0.0));
                const double segLength = math::length(seg.endMetres - seg.startMetres);
                CHECK(segLength == doctest::Approx(0.03).epsilon(1e-12));
                // Capped before reaching -0.075 m
                CHECK(seg.endMetres.z > -0.075);
            }
        }
        CHECK(virtualCount == options.rayCount);
    }

    // Test 2: virtualExtensionDistanceMetres = 0.50 m (>= 0.075 m) allows full reach to virtual image plane
    {
        ray::BenchTracerOptions options;
        options.rayCount = 10;
        options.pattern = ray::RaySamplingPattern::MeridionalFan;
        options.includeVirtualExtensions = true;
        options.virtualExtensionDistanceMetres = 0.50;

        const auto segments = ray::traceBench(scene, options);
        std::size_t virtualCount = 0;
        for (const auto& seg : segments) {
            if (seg.kind == ray::RaySegmentKind::VirtualExtension) {
                ++virtualCount;
                CHECK(seg.startMetres.z == doctest::Approx(0.0));
                CHECK(seg.endMetres.z == doctest::Approx(-0.075).epsilon(1e-12));
            }
        }
        CHECK(virtualCount == options.rayCount);
    }
}

TEST_CASE("diverging lens always produces virtual image") {
    auto scene = scene::createDefaultRealImageScene();
    scene.lens.focalLengthMetres = -0.05; // diverging lens
    scene.source.positionMetres = {0.003, 0.006, -0.10};
    // u = 0.10, f = -0.05 => 1/v = -20 - 10 = -30 => v = -1/30 = -0.0333333 m
    // m = -(-1/30)/0.10 = +1/3

    const auto prediction = scene::predictThinLensImage(scene);
    REQUIRE(prediction.nature == scene::ImageNature::Virtual);
    CHECK(prediction.imageDistanceMetres == doctest::Approx(-1.0 / 30.0));
    CHECK(prediction.transverseMagnification == doctest::Approx(1.0 / 3.0));
    CHECK(prediction.imagePositionMetres.x == doctest::Approx(0.001).epsilon(1e-12));
    CHECK(prediction.imagePositionMetres.y == doctest::Approx(0.002).epsilon(1e-12));
}

TEST_CASE("object at focal point produces image at infinity and collimated rays") {
    auto scene = scene::createDefaultInfinityScene();
    scene.source.positionMetres = {0.0, 0.0, -0.05};
    scene.lens.focalLengthMetres = 0.05;

    const auto predictionExact = scene::predictThinLensImage(scene);
    CHECK(predictionExact.nature == scene::ImageNature::AtInfinity);
    CHECK(std::isinf(predictionExact.imageDistanceMetres));
    CHECK(std::isinf(predictionExact.transverseMagnification));

    // Scale-dependent tolerance: near focus perturbation is treated as AtInfinity
    scene.source.positionMetres.z = -0.05 + 1e-11;
    const auto predictionNear = scene::predictThinLensImage(scene);
    CHECK(predictionNear.nature == scene::ImageNature::AtInfinity);

    // Tracing from focal point produces parallel forward rays
    scene.source.positionMetres = {0.0, 0.0, -0.05};
    scene.screen.planeZMetres = 0.20;
    ray::BenchTracerOptions options;
    options.rayCount = 16;
    options.pattern = ray::RaySamplingPattern::MeridionalFan;

    const auto segments = ray::traceBench(scene, options);
    for (const auto& seg : segments) {
        if (seg.kind == ray::RaySegmentKind::Transmitted) {
            // Rays parallel to optical axis maintain constant (x, y)
            CHECK(seg.endMetres.x == doctest::Approx(seg.startMetres.x).epsilon(1e-12));
            CHECK(seg.endMetres.y == doctest::Approx(seg.startMetres.y).epsilon(1e-12));
        }
    }
}

TEST_CASE("magnification obeys analytic formula across different object distances") {
    constexpr double f = 0.05;
    const std::vector<double> objectDistances {0.10, 0.15, 0.20, 0.25, 0.50};

    auto scene = scene::createDefaultRealImageScene();
    scene.lens.focalLengthMetres = f;
    constexpr double objectY = 0.008;

    for (const double u : objectDistances) {
        scene.source.positionMetres = {0.0, objectY, -u};
        const auto prediction = scene::predictThinLensImage(scene);
        const double expectedV = (u * f) / (u - f);
        const double expectedM = -expectedV / u;
        const double expectedY = expectedM * objectY;

        CHECK(prediction.imageDistanceMetres == doctest::Approx(expectedV));
        CHECK(prediction.transverseMagnification == doctest::Approx(expectedM));
        CHECK(prediction.imagePositionMetres.y == doctest::Approx(expectedY));
    }
}

TEST_CASE("bench tracer is strictly deterministic") {
    const auto scene = scene::createDefaultRealImageScene();
    ray::BenchTracerOptions options;
    options.rayCount = 128;
    options.pattern = ray::RaySamplingPattern::FibonacciDisk;

    std::vector<ray::RaySegment> run1;
    std::vector<ray::RaySegment> run2;
    ray::traceBench(scene, options, run1);
    ray::traceBench(scene, options, run2);

    REQUIRE(run1.size() == run2.size());
    for (std::size_t i = 0; i < run1.size(); ++i) {
        CHECK(run1[i] == run2[i]);
    }
}

TEST_CASE("aperture coplanar with lens plane clips rays outside aperture radius") {
    auto scene = scene::createDefaultRealImageScene();
    scene.lens.clearApertureRadiusMetres = 0.020;
    scene.aperture.radiusMetres = 0.010; // aperture stop restricts to 10mm
    scene.aperture.planeZMetres = scene.lens.planeZMetres;

    ray::BenchTracerOptions options;
    options.rayCount = 21;
    options.pattern = ray::RaySamplingPattern::MeridionalFan;

    const auto segments = ray::traceBench(scene, options);

    std::size_t clippedCount = 0;
    std::size_t transmittedCount = 0;
    std::size_t incidentCount = 0;

    for (const auto& seg : segments) {
        if (seg.kind == ray::RaySegmentKind::Clipped) {
            const double y = seg.endMetres.y;
            CHECK(std::abs(y) > 0.010 - 1e-12);
            CHECK(seg.startMetres.z == doctest::Approx(-0.15));
            CHECK(seg.endMetres.z == doctest::Approx(0.0));
            ++clippedCount;
        } else if (seg.kind == ray::RaySegmentKind::Incident) {
            const double y = seg.endMetres.y;
            CHECK(std::abs(y) <= 0.010 + 1e-12);
            ++incidentCount;
        } else if (seg.kind == ray::RaySegmentKind::Transmitted) {
            ++transmittedCount;
        }
    }

    CHECK(clippedCount > 0);
    CHECK(transmittedCount > 0);
    CHECK(incidentCount == transmittedCount);
    CHECK(transmittedCount + clippedCount == options.rayCount);
}

TEST_CASE("aperture before lens clips rays at aperture plane") {
    auto scene = scene::createDefaultRealImageScene();
    // Source at z = -0.15, lens at z = 0.0
    // Place standalone aperture in front of lens at z = -0.075 with radius 0.005
    scene.lens.clearApertureRadiusMetres = 0.020;
    scene.aperture.planeZMetres = -0.075;
    scene.aperture.radiusMetres = 0.005;

    ray::BenchTracerOptions options;
    options.rayCount = 21;
    options.pattern = ray::RaySamplingPattern::MeridionalFan;

    const auto segments = ray::traceBench(scene, options);

    std::size_t clippedCount = 0;
    std::size_t incidentCount = 0;
    std::size_t transmittedCount = 0;

    for (const auto& seg : segments) {
        if (seg.kind == ray::RaySegmentKind::Clipped) {
            // Ray should be stopped at the aperture plane z = -0.075
            CHECK(seg.startMetres.z == doctest::Approx(-0.15));
            CHECK(seg.endMetres.z == doctest::Approx(-0.075));
            const double yAtAp = seg.endMetres.y;
            CHECK(std::abs(yAtAp) > 0.005 - 1e-12);
            ++clippedCount;
        } else if (seg.kind == ray::RaySegmentKind::Incident) {
            // Unclipped incident rays reach the lens plane z = 0.0
            CHECK(seg.startMetres.z == doctest::Approx(-0.15));
            CHECK(seg.endMetres.z == doctest::Approx(0.0));
            ++incidentCount;
        } else if (seg.kind == ray::RaySegmentKind::Transmitted) {
            // Unclipped rays continue to the screen at z = 0.075
            CHECK(seg.startMetres.z == doctest::Approx(0.0));
            CHECK(seg.endMetres.z == doctest::Approx(0.075));
            ++transmittedCount;
        }
    }

    CHECK(clippedCount > 0);
    CHECK(transmittedCount > 0);
    CHECK(incidentCount == transmittedCount);
    CHECK(transmittedCount + clippedCount == options.rayCount);
}

TEST_CASE("aperture after lens and before screen clips transmitted rays at aperture plane") {
    auto scene = scene::createDefaultRealImageScene();
    // Lens at z = 0.0, screen at z = 0.075
    // Place standalone aperture between lens and screen at z = 0.04 with restrictive radius 0.005
    scene.aperture.planeZMetres = 0.04;
    scene.aperture.radiusMetres = 0.005;

    ray::BenchTracerOptions options;
    options.rayCount = 21;
    options.pattern = ray::RaySamplingPattern::MeridionalFan;

    const auto segments = ray::traceBench(scene, options);

    std::size_t incidentCount = 0;
    std::size_t clippedCount = 0;
    std::size_t transmittedCount = 0;

    for (const auto& seg : segments) {
        if (seg.kind == ray::RaySegmentKind::Incident) {
            // All sampled rays reach the lens since clear aperture is large
            CHECK(seg.startMetres.z == doctest::Approx(-0.15));
            CHECK(seg.endMetres.z == doctest::Approx(0.0));
            ++incidentCount;
        } else if (seg.kind == ray::RaySegmentKind::Clipped) {
            // Rays stopped downstream at aperture z = 0.04
            CHECK(seg.startMetres.z == doctest::Approx(0.0));
            CHECK(seg.endMetres.z == doctest::Approx(0.04));
            const double yAtAp = seg.endMetres.y;
            CHECK(std::abs(yAtAp) > 0.005 - 1e-12);
            ++clippedCount;
        } else if (seg.kind == ray::RaySegmentKind::Transmitted) {
            // Unclipped rays continue to screen at z = 0.075
            CHECK(seg.startMetres.z == doctest::Approx(0.0));
            CHECK(seg.endMetres.z == doctest::Approx(0.075));
            ++transmittedCount;
        }
    }

    CHECK(incidentCount == options.rayCount);
    CHECK(clippedCount > 0);
    CHECK(transmittedCount > 0);
    CHECK(transmittedCount + clippedCount == options.rayCount);
}

TEST_CASE("aperture positioned behind screen does not affect rays already reaching screen") {
    auto scene = scene::createDefaultRealImageScene();
    // Screen is at z = 0.075, place tiny aperture downstream at z = 0.15
    scene.screen.planeZMetres = 0.075;
    scene.aperture.planeZMetres = 0.15;
    scene.aperture.radiusMetres = 0.0001; // tiny radius, but downstream of screen

    ray::BenchTracerOptions options;
    options.rayCount = 20;
    options.pattern = ray::RaySamplingPattern::FibonacciDisk;

    const auto segments = ray::traceBench(scene, options);

    std::size_t transmittedCount = 0;
    std::size_t clippedCount = 0;

    for (const auto& seg : segments) {
        if (seg.kind == ray::RaySegmentKind::Transmitted) {
            CHECK(seg.endMetres.z == doctest::Approx(0.075));
            ++transmittedCount;
        } else if (seg.kind == ray::RaySegmentKind::Clipped) {
            ++clippedCount;
        }
    }

    // All rays reached the screen; aperture downstream did not clip any ray
    CHECK(transmittedCount == options.rayCount);
    CHECK(clippedCount == 0);
}

TEST_CASE("repeated tracing with 10,000 rays preserves vector capacity without reallocation") {
    const auto scene = scene::createDefaultRealImageScene();
    ray::BenchTracerOptions options;
    options.rayCount = 10000;
    options.pattern = ray::RaySamplingPattern::FibonacciDisk;
    options.includeVirtualExtensions = false;

    std::vector<ray::RaySegment> buffer;

    // First call will reserve needed capacity
    ray::traceBench(scene, options, buffer);
    REQUIRE(buffer.size() == 20000); // 10,000 incident + 10,000 transmitted
    const std::size_t initialCapacity = buffer.capacity();
    const ray::RaySegment* initialDataPtr = buffer.data();

    // Subsequent calls must reuse buffer with zero reallocations
    for (int iter = 0; iter < 50; ++iter) {
        ray::traceBench(scene, options, buffer);
        CHECK(buffer.size() == 20000);
        CHECK(buffer.capacity() == initialCapacity);
        CHECK(buffer.data() == initialDataPtr);
    }
}

TEST_CASE("optical bench rejects duplicate component IDs across all pairs") {
    const auto baseline = scene::createDefaultRealImageScene();
    CHECK(scene::isSceneValid(baseline));

    // source == lens
    auto dupSourceLens = baseline;
    dupSourceLens.source.id = "same_id";
    dupSourceLens.lens.id = "same_id";
    CHECK_THROWS_AS(scene::validateScene(dupSourceLens), std::invalid_argument);

    // source == aperture
    auto dupSourceAp = baseline;
    dupSourceAp.source.id = "same_id";
    dupSourceAp.aperture.id = "same_id";
    CHECK_THROWS_AS(scene::validateScene(dupSourceAp), std::invalid_argument);

    // source == screen
    auto dupSourceScreen = baseline;
    dupSourceScreen.source.id = "same_id";
    dupSourceScreen.screen.id = "same_id";
    CHECK_THROWS_AS(scene::validateScene(dupSourceScreen), std::invalid_argument);

    // lens == aperture
    auto dupLensAp = baseline;
    dupLensAp.lens.id = "same_id";
    dupLensAp.aperture.id = "same_id";
    CHECK_THROWS_AS(scene::validateScene(dupLensAp), std::invalid_argument);

    // lens == screen
    auto dupLensScreen = baseline;
    dupLensScreen.lens.id = "same_id";
    dupLensScreen.screen.id = "same_id";
    CHECK_THROWS_AS(scene::validateScene(dupLensScreen), std::invalid_argument);

    // aperture == screen
    auto dupApScreen = baseline;
    dupApScreen.aperture.id = "same_id";
    dupApScreen.screen.id = "same_id";
    CHECK_THROWS_AS(scene::validateScene(dupApScreen), std::invalid_argument);
}

TEST_CASE("scene and tracer validate inputs, check overflow and reject illegal parameters") {
    // Valid baseline
    auto scene = scene::createDefaultRealImageScene();
    CHECK(scene::isSceneValid(scene));

    // Empty IDs
    auto invalidId = scene;
    invalidId.source.id = "";
    CHECK_THROWS_AS(scene::validateScene(invalidId), std::invalid_argument);
    invalidId = scene;
    invalidId.lens.id = "";
    CHECK_THROWS_AS(scene::validateScene(invalidId), std::invalid_argument);
    invalidId = scene;
    invalidId.aperture.id = "";
    CHECK_THROWS_AS(scene::validateScene(invalidId), std::invalid_argument);
    invalidId = scene;
    invalidId.screen.id = "";
    CHECK_THROWS_AS(scene::validateScene(invalidId), std::invalid_argument);

    // Non-finite values
    auto nonFinite = scene;
    nonFinite.source.positionMetres.x = std::numeric_limits<double>::quiet_NaN();
    CHECK_THROWS_AS(scene::validateScene(nonFinite), std::invalid_argument);

    // Zero / negative focal length / dimensions
    auto zeroFocal = scene;
    zeroFocal.lens.focalLengthMetres = 0.0;
    CHECK_THROWS_AS(scene::validateScene(zeroFocal), std::invalid_argument);

    auto negAperture = scene;
    negAperture.lens.clearApertureRadiusMetres = -0.01;
    CHECK_THROWS_AS(scene::validateScene(negAperture), std::invalid_argument);

    auto negWavelength = scene;
    negWavelength.source.wavelengthMetres = 0.0;
    CHECK_THROWS_AS(scene::validateScene(negWavelength), std::invalid_argument);

    auto negPower = scene;
    negPower.source.powerWatts = -1.0;
    CHECK_THROWS_AS(scene::validateScene(negPower), std::invalid_argument);

    auto negScreen = scene;
    negScreen.screen.widthMetres = 0.0;
    CHECK_THROWS_AS(scene::validateScene(negScreen), std::invalid_argument);

    // Geometric ordering: source placed behind or on lens
    auto badGeometry = scene;
    badGeometry.source.positionMetres.z = scene.lens.planeZMetres + 0.01;
    CHECK_THROWS_AS(scene::validateScene(badGeometry), std::invalid_argument);

    // Tracer options validation
    ray::BenchTracerOptions badOptions;
    badOptions.rayCount = 0;
    std::vector<ray::RaySegment> segs;
    CHECK_THROWS_AS(ray::traceBench(scene, badOptions, segs), std::invalid_argument);

    badOptions = {};
    badOptions.maxPropagationDistanceMetres = -1.0;
    CHECK_THROWS_AS(ray::traceBench(scene, badOptions, segs), std::invalid_argument);

    badOptions = {};
    badOptions.virtualExtensionDistanceMetres = -0.5;
    CHECK_THROWS_AS(ray::traceBench(scene, badOptions, segs), std::invalid_argument);

    // Capacity overflow prevention with illegal oversized input
    badOptions = {};
    badOptions.rayCount = std::numeric_limits<std::size_t>::max();
    CHECK_THROWS_AS(ray::traceBench(scene, badOptions, segs), std::invalid_argument);

    // Invalid ray sampling pattern (negative and out-of-range positive values)
    const std::vector<ray::RaySegment> originalSegments {
        ray::RaySegment {
            .startMetres = {0.001, 0.002, -0.05},
            .endMetres = {0.003, 0.004, 0.0},
            .wavelengthMetres = 532e-9,
            .power = 0.8,
            .kind = ray::RaySegmentKind::Incident,
            .rayIndex = 12,
        },
        ray::RaySegment {
            .startMetres = {0.003, 0.004, 0.0},
            .endMetres = {0.005, 0.006, 0.1},
            .wavelengthMetres = 532e-9,
            .power = 0.8,
            .kind = ray::RaySegmentKind::Transmitted,
            .rayIndex = 12,
        },
    };

    const int invalidPatternValues[] = {-100, -1, 6, 7, 42, 999};
    for (const int rawVal : invalidPatternValues) {
        badOptions = {};
        badOptions.pattern = static_cast<ray::RaySamplingPattern>(rawVal);
        std::vector<ray::RaySegment> callerBuffer = originalSegments;
        CHECK_THROWS_AS(ray::traceBench(scene, badOptions, callerBuffer), std::invalid_argument);
        CHECK(callerBuffer == originalSegments);
    }
}
