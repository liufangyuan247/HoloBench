#include <doctest/doctest.h>

#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>
#include <vector>

#include "optics/ray/GeometricElements.hpp"
#include "optics/scene/GeometricComponents.hpp"

namespace math = holobench::math;
namespace ray = holobench::optics::ray;
namespace scene = holobench::optics::scene;

namespace {

constexpr double kPi = std::numbers::pi_v<double>;

double degToRad(double deg) {
    return deg * (kPi / 180.0);
}

} // namespace

TEST_CASE("analytic forward plane intersection computes exact distances and points") {
    // Subcase 1: Normal incident ray along +Z hitting plane at Z = 0.1
    {
        const auto r = ray::makeRay({0.02, -0.03, -0.15}, {0.0, 0.0, 1.0});
        const math::Vec3d planePoint {0.0, 0.0, 0.10};
        const math::Vec3d planeNormal {0.0, 0.0, -1.0};

        const auto isect = ray::intersectPlaneForward(r, planePoint, planeNormal);
        REQUIRE(isect.hit);
        CHECK(isect.signedDistanceMetres == doctest::Approx(0.25).epsilon(1e-12));
        CHECK(isect.pointMetres.x == doctest::Approx(0.02).epsilon(1e-12));
        CHECK(isect.pointMetres.y == doctest::Approx(-0.03).epsilon(1e-12));
        CHECK(isect.pointMetres.z == doctest::Approx(0.10).epsilon(1e-12));

        const auto optPoint = ray::intersectForwardPlane(r, planePoint, planeNormal);
        REQUIRE(optPoint.has_value());
        CHECK(optPoint->z == doctest::Approx(0.10).epsilon(1e-12));
    }

    // Subcase 2: Oblique ray hitting 45-degree plane
    {
        const auto r = ray::makeRay({0.0, 0.0, 0.0}, {0.0, 0.0, 1.0});
        const math::Vec3d planePoint {0.0, 0.0, 0.20};
        // Plane tilted 45 degrees around Y axis
        const math::Vec3d planeNormal {-1.0 / std::numbers::sqrt2_v<double>, 0.0, -1.0 / std::numbers::sqrt2_v<double>};

        const auto isect = ray::intersectPlaneForward(r, planePoint, planeNormal);
        REQUIRE(isect.hit);
        CHECK(isect.signedDistanceMetres == doctest::Approx(0.20).epsilon(1e-12));
        CHECK(isect.pointMetres.x == doctest::Approx(0.0).epsilon(1e-12));
        CHECK(isect.pointMetres.y == doctest::Approx(0.0).epsilon(1e-12));
        CHECK(isect.pointMetres.z == doctest::Approx(0.20).epsilon(1e-12));
    }
}

TEST_CASE("plane intersection rejects parallel, near-parallel, and behind-origin rays") {
    const math::Vec3d planePoint {0.0, 0.0, 0.10};
    const math::Vec3d planeNormal {0.0, 0.0, -1.0};

    // Parallel ray (perpendicular to normal)
    {
        const auto r = ray::makeRay({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0});
        const auto isect = ray::intersectPlaneForward(r, planePoint, planeNormal);
        CHECK_FALSE(isect.hit);
        CHECK_FALSE(ray::intersectForwardPlane(r, planePoint, planeNormal).has_value());
    }

    // Near-parallel ray within tolerance (dot <= 1e-12)
    {
        const auto r = ray::makeRay({0.0, 0.0, 0.0}, {1.0, 0.0, 1e-13});
        const auto isect = ray::intersectPlaneForward(r, planePoint, planeNormal);
        CHECK_FALSE(isect.hit);
    }

    // Ray pointing away from plane (plane is behind ray origin)
    {
        const auto r = ray::makeRay({0.0, 0.0, 0.20}, {0.0, 0.0, 1.0});
        const auto isect = ray::intersectPlaneForward(r, planePoint, planeNormal);
        CHECK_FALSE(isect.hit);
        CHECK(isect.signedDistanceMetres < 0.0);
    }

    // Ray starting on plane pointing away from plane
    {
        const auto r = ray::makeRay({0.0, 0.0, 0.10}, {0.0, 0.0, -1.0});
        const auto isect = ray::intersectPlaneForward(r, planePoint, planeNormal);
        CHECK_FALSE(isect.hit);
    }
}

TEST_CASE("rectangular aperture clipping correctly classifies interior, boundary, and exterior") {
    const math::Vec3d planePoint {0.0, 0.0, 0.0};
    const math::Vec3d planeNormal {0.0, 0.0, 1.0};
    constexpr double width = 0.04;
    constexpr double height = 0.06;

    // Interior points
    CHECK(ray::isInsideRectangularAperture({0.0, 0.0, 0.0}, planePoint, planeNormal, width, height));
    CHECK(ray::isInsideRectangularAperture({0.019, 0.029, 0.0}, planePoint, planeNormal, width, height));
    CHECK(ray::isInsideRectangularAperture({-0.019, -0.029, 0.0}, planePoint, planeNormal, width, height));

    // Exact edge points (within tolerance)
    CHECK(ray::isInsideRectangularAperture({0.020, 0.0, 0.0}, planePoint, planeNormal, width, height));
    CHECK(ray::isInsideRectangularAperture({-0.020, 0.0, 0.0}, planePoint, planeNormal, width, height));
    CHECK(ray::isInsideRectangularAperture({0.0, 0.030, 0.0}, planePoint, planeNormal, width, height));
    CHECK(ray::isInsideRectangularAperture({0.0, -0.030, 0.0}, planePoint, planeNormal, width, height));
    CHECK(ray::isInsideRectangularAperture({0.020, 0.030, 0.0}, planePoint, planeNormal, width, height));

    // Exterior points
    CHECK_FALSE(ray::isInsideRectangularAperture({0.021, 0.0, 0.0}, planePoint, planeNormal, width, height));
    CHECK_FALSE(ray::isInsideRectangularAperture({-0.021, 0.0, 0.0}, planePoint, planeNormal, width, height));
    CHECK_FALSE(ray::isInsideRectangularAperture({0.0, 0.031, 0.0}, planePoint, planeNormal, width, height));
    CHECK_FALSE(ray::isInsideRectangularAperture({0.0, -0.031, 0.0}, planePoint, planeNormal, width, height));
    CHECK_FALSE(ray::isInsideRectangularAperture({0.05, 0.05, 0.0}, planePoint, planeNormal, width, height));
}

TEST_CASE("ideal mirror reflection obeys specular reflection law and distinguishes status") {
    scene::PlanarMirror mirror;
    mirror.planePointMetres = {0.0, 0.0, 0.10};
    mirror.widthMetres = 0.08;
    mirror.heightMetres = 0.08;

    // 45-degree fold mirror turning +Z ray into +X
    mirror.normal = {1.0 / std::numbers::sqrt2_v<double>, 0.0, -1.0 / std::numbers::sqrt2_v<double>};
    const auto incidentZ = ray::makeRay({0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, 632.8e-9, 2.5);

    const auto resFold = ray::tracePlanarMirror(incidentZ, mirror);
    REQUIRE(resFold.status == ray::GeometricInteractionStatus::Reflected);
    REQUIRE(resFold.outgoingRay.has_value());
    CHECK(resFold.distanceMetres == doctest::Approx(0.10).epsilon(1e-12));
    CHECK(resFold.intersectionMetres.z == doctest::Approx(0.10).epsilon(1e-12));

    // Reflected ray must propagate purely along +X
    CHECK(resFold.outgoingRay->direction.x == doctest::Approx(1.0).epsilon(1e-12));
    CHECK(resFold.outgoingRay->direction.y == doctest::Approx(0.0).epsilon(1e-12));
    CHECK(resFold.outgoingRay->direction.z == doctest::Approx(0.0).epsilon(1e-12));
    CHECK(resFold.outgoingRay->wavelengthMetres == doctest::Approx(632.8e-9).epsilon(1e-12));
    CHECK(resFold.outgoingRay->power == doctest::Approx(2.5).epsilon(1e-12));

    // Normal orientation invariance (+N vs -N yields same specular reflection)
    {
        auto mirrorFlipped = mirror;
        mirrorFlipped.normal = -mirror.normal;
        const auto resFlipped = ray::tracePlanarMirror(incidentZ, mirrorFlipped);
        REQUIRE(resFlipped.status == ray::GeometricInteractionStatus::Reflected);
        REQUIRE(resFlipped.outgoingRay.has_value());
        CHECK(resFlipped.outgoingRay->direction.x == doctest::Approx(1.0).epsilon(1e-12));
        CHECK(resFlipped.outgoingRay->direction.z == doctest::Approx(0.0).epsilon(1e-12));
    }

    // Ray hitting outside mirror aperture gives Clipped
    {
        const auto offAxisRay = ray::makeRay({0.08, 0.0, 0.0}, {0.0, 0.0, 1.0});
        const auto resClipped = ray::tracePlanarMirror(offAxisRay, mirror);
        CHECK(resClipped.status == ray::GeometricInteractionStatus::Clipped);
        CHECK_FALSE(resClipped.outgoingRay.has_value());
        CHECK(resClipped.distanceMetres == doctest::Approx(0.18).epsilon(1e-12));
    }

    // Ray pointing away gives Miss
    {
        const auto awayRay = ray::makeRay({0.0, 0.0, 0.0}, {0.0, 0.0, -1.0});
        const auto resMiss = ray::tracePlanarMirror(awayRay, mirror);
        CHECK(resMiss.status == ray::GeometricInteractionStatus::Miss);
        CHECK_FALSE(resMiss.outgoingRay.has_value());
    }
}

TEST_CASE("plane dielectric interface distinguishes Refracted, TotalInternalReflection, Clipped, and Miss") {
    scene::PlaneInterfaceComponent interfaceComp;
    interfaceComp.planePointMetres = {0.0, 0.0, 0.0};
    interfaceComp.normal = {0.0, 0.0, -1.0};
    interfaceComp.widthMetres = 0.30;
    interfaceComp.heightMetres = 0.30;

    // Subcase 1: Air to glass 30-degree refraction
    interfaceComp.nIncident = 1.0;
    interfaceComp.nTransmitted = 1.5;
    const double thetaI = degToRad(30.0);
    const auto ray30 = ray::makeRay({0.0, 0.0, -0.10}, {std::sin(thetaI), 0.0, std::cos(thetaI)});

    const auto resRefr = ray::tracePlaneInterface(ray30, interfaceComp);
    REQUIRE(resRefr.status == ray::GeometricInteractionStatus::Refracted);
    REQUIRE(resRefr.outgoingRay.has_value());
    // Snell law: 1.0 * sin(30 deg) = 1.5 * sin(theta_t) => sin(theta_t) = 1/3
    CHECK(resRefr.outgoingRay->direction.x == doctest::Approx(1.0 / 3.0).epsilon(1e-12));
    CHECK(resRefr.outgoingRay->direction.z > 0.0);

    // Subcase 2: Glass to air TIR at 50 degrees (critical angle ~41.81 degrees)
    interfaceComp.nIncident = 1.5;
    interfaceComp.nTransmitted = 1.0;
    const double thetaTIR = degToRad(50.0);
    const auto ray50 = ray::makeRay({0.0, 0.0, -0.10}, {std::sin(thetaTIR), 0.0, std::cos(thetaTIR)});

    const auto resTIR = ray::tracePlaneInterface(ray50, interfaceComp);
    REQUIRE(resTIR.status == ray::GeometricInteractionStatus::TotalInternalReflection);
    REQUIRE(resTIR.outgoingRay.has_value());
    // TIR reflects back with z < 0
    CHECK(resTIR.outgoingRay->direction.x == doctest::Approx(std::sin(thetaTIR)).epsilon(1e-12));
    CHECK(resTIR.outgoingRay->direction.z == doctest::Approx(-std::cos(thetaTIR)).epsilon(1e-12));

    // Subcase 3: Hit outside interface aperture gives Clipped
    {
        const auto rayOutside = ray::makeRay({0.20, 0.0, -0.10}, {0.0, 0.0, 1.0});
        const auto resClipped = ray::tracePlaneInterface(rayOutside, interfaceComp);
        CHECK(resClipped.status == ray::GeometricInteractionStatus::Clipped);
        CHECK_FALSE(resClipped.outgoingRay.has_value());
    }

    // Subcase 4: Ray pointing away gives Miss
    {
        const auto rayAway = ray::makeRay({0.0, 0.0, -0.10}, {0.0, 0.0, -1.0});
        const auto resMiss = ray::tracePlaneInterface(rayAway, interfaceComp);
        CHECK(resMiss.status == ray::GeometricInteractionStatus::Miss);
        CHECK_FALSE(resMiss.outgoingRay.has_value());
    }
}

TEST_CASE("collimated ray bundle emission guarantees parallelism, power conservation, and disk radius") {
    scene::CollimatedSource source;
    source.originMetres = {0.01, -0.02, -0.20};
    source.direction = math::normalized(math::Vec3d {0.1, -0.2, 1.0});
    source.beamRadiusMetres = 0.015;
    source.wavelengthMetres = 488e-9;
    source.powerWatts = 3.6;

    for (const auto pattern : {
             ray::CollimatedRayPattern::FibonacciDisk,
             ray::CollimatedRayPattern::ConcentricRings,
             ray::CollimatedRayPattern::UniformGrid,
         }) {
        constexpr std::size_t rayCount = 100;
        const auto rays = ray::emitCollimatedRays(source, rayCount, pattern);
        REQUIRE(rays.size() == rayCount);

        double totalPower = 0.0;
        const auto basis = ray::computePlanarApertureBasis(source.direction);

        for (const auto& r : rays) {
            // 1. Direction strictly parallel and normalized
            CHECK(r.direction.x == doctest::Approx(source.direction.x).epsilon(1e-12));
            CHECK(r.direction.y == doctest::Approx(source.direction.y).epsilon(1e-12));
            CHECK(r.direction.z == doctest::Approx(source.direction.z).epsilon(1e-12));
            CHECK(math::length(r.direction) == doctest::Approx(1.0).epsilon(1e-12));

            // 2. Wavelength preserved
            CHECK(r.wavelengthMetres == doctest::Approx(source.wavelengthMetres).epsilon(1e-12));

            // 3. Disk radius boundary: offset from origin projected on basis <= beam radius
            const math::Vec3d delta = r.originMetres - source.originMetres;
            const double u = math::dot(delta, basis.uAxis);
            const double v = math::dot(delta, basis.vAxis);
            const double radius = std::sqrt(u * u + v * v);
            CHECK(radius <= source.beamRadiusMetres + 1e-12);

            totalPower += r.power;
        }

        // 4. Strict total power conservation
        CHECK(totalPower == doctest::Approx(source.powerWatts).epsilon(1e-12));
    }
}

TEST_CASE("collimated emission is deterministic and reuses 10,000 ray buffer without reallocation") {
    scene::CollimatedSource source;
    source.originMetres = {0.0, 0.0, -0.10};
    source.direction = {0.0, 0.0, 1.0};
    source.beamRadiusMetres = 0.02;
    source.powerWatts = 5.0;

    constexpr std::size_t rayCount = 10000;
    std::vector<ray::Ray> buffer;

    // First call allocates
    ray::emitCollimatedRays(source, rayCount, buffer, ray::CollimatedRayPattern::FibonacciDisk);
    REQUIRE(buffer.size() == rayCount);
    const std::size_t initialCapacity = buffer.capacity();
    const ray::Ray* initialPtr = buffer.data();

    // Verify determinism by taking snapshot
    const auto snapshot = buffer;

    // 50 subsequent calls must reuse buffer memory with zero reallocations and bitwise identical output
    for (int iter = 0; iter < 50; ++iter) {
        ray::emitCollimatedRays(source, rayCount, buffer, ray::CollimatedRayPattern::FibonacciDisk);
        CHECK(buffer.size() == rayCount);
        CHECK(buffer.capacity() == initialCapacity);
        CHECK(buffer.data() == initialPtr);
    }

    for (std::size_t i = 0; i < rayCount; ++i) {
        CHECK(buffer[i].originMetres.x == snapshot[i].originMetres.x);
        CHECK(buffer[i].originMetres.y == snapshot[i].originMetres.y);
        CHECK(buffer[i].originMetres.z == snapshot[i].originMetres.z);
        CHECK(buffer[i].direction.x == snapshot[i].direction.x);
        CHECK(buffer[i].direction.y == snapshot[i].direction.y);
        CHECK(buffer[i].direction.z == snapshot[i].direction.z);
        CHECK(buffer[i].power == snapshot[i].power);
    }
}

TEST_CASE("semantic validation rejects illegal parameters, non-finite values, and duplicate IDs") {
    // 1. CollimatedSource validation
    {
        auto src = scene::createDefaultCollimatedSource();
        CHECK(scene::isCollimatedSourceValid(src));

        // Empty ID
        src.id = "";
        CHECK_THROWS_AS(scene::validateCollimatedSource(src), std::invalid_argument);
        src = scene::createDefaultCollimatedSource();

        // Non-finite origin
        src.originMetres.x = std::numeric_limits<double>::quiet_NaN();
        CHECK_THROWS_AS(scene::validateCollimatedSource(src), std::invalid_argument);
        src = scene::createDefaultCollimatedSource();

        // Zero direction
        src.direction = {0.0, 0.0, 0.0};
        CHECK_THROWS_AS(scene::validateCollimatedSource(src), std::invalid_argument);
        src = scene::createDefaultCollimatedSource();

        // Zero / negative beam radius
        src.beamRadiusMetres = 0.0;
        CHECK_THROWS_AS(scene::validateCollimatedSource(src), std::invalid_argument);
        src.beamRadiusMetres = -0.01;
        CHECK_THROWS_AS(scene::validateCollimatedSource(src), std::invalid_argument);
        src = scene::createDefaultCollimatedSource();

        // Negative power
        src.powerWatts = -1.0;
        CHECK_THROWS_AS(scene::validateCollimatedSource(src), std::invalid_argument);
    }

    // 2. PlanarMirror validation
    {
        auto mirror = scene::createDefaultPlanarMirror();
        CHECK(scene::isPlanarMirrorValid(mirror));

        mirror.id = "";
        CHECK_THROWS_AS(scene::validatePlanarMirror(mirror), std::invalid_argument);
        mirror = scene::createDefaultPlanarMirror();

        mirror.normal = {0.0, 0.0, 0.0};
        CHECK_THROWS_AS(scene::validatePlanarMirror(mirror), std::invalid_argument);
        mirror = scene::createDefaultPlanarMirror();

        mirror.widthMetres = 0.0;
        CHECK_THROWS_AS(scene::validatePlanarMirror(mirror), std::invalid_argument);
        mirror.heightMetres = -0.05;
        CHECK_THROWS_AS(scene::validatePlanarMirror(mirror), std::invalid_argument);
    }

    // 3. PlaneInterfaceComponent validation
    {
        auto ifComp = scene::createDefaultPlaneInterface();
        CHECK(scene::isPlaneInterfaceComponentValid(ifComp));

        ifComp.nIncident = 0.0;
        CHECK_THROWS_AS(scene::validatePlaneInterfaceComponent(ifComp), std::invalid_argument);
        ifComp = scene::createDefaultPlaneInterface();

        ifComp.nTransmitted = -1.5;
        CHECK_THROWS_AS(scene::validatePlaneInterfaceComponent(ifComp), std::invalid_argument);
    }

    // 4. Duplicate ID validation helper
    {
        CHECK_NOTHROW(scene::validateUniqueComponentIds({"src_1", "mirror_1", "if_1"}));
        CHECK_THROWS_AS(scene::validateUniqueComponentIds({"src_1", "mirror_1", "src_1"}), std::invalid_argument);
        CHECK_THROWS_AS(scene::validateUniqueComponentIds({"src_1", ""}), std::invalid_argument);
    }

    // 5. emitCollimatedRays input rejection
    {
        const auto src = scene::createDefaultCollimatedSource();
        std::vector<ray::Ray> rays;

        // Zero rayCount
        CHECK_THROWS_AS(ray::emitCollimatedRays(src, 0, rays), std::invalid_argument);

        // Overflow rayCount
        CHECK_THROWS_AS(ray::emitCollimatedRays(src, ray::kMaxCollimatedRayCount + 1, rays), std::invalid_argument);

        // Invalid pattern enum
        CHECK_THROWS_AS(ray::emitCollimatedRays(src, 10, rays, static_cast<ray::CollimatedRayPattern>(-1)), std::invalid_argument);
        CHECK_THROWS_AS(ray::emitCollimatedRays(src, 10, rays, static_cast<ray::CollimatedRayPattern>(99)), std::invalid_argument);
    }
}

TEST_CASE("plane intersection strictly rejects t=0 and non-forward distances when epsilon is zero or positive") {
    const math::Vec3d planePoint {0.0, 0.0, 0.0};
    const math::Vec3d planeNormal {0.0, 0.0, 1.0};

    // Case 1: Ray origin lies precisely on plane (t = 0.0) with epsilon = 0.0 -> must be rejected
    {
        const auto r = ray::makeRay({0.0, 0.0, 0.0}, {0.0, 0.0, 1.0});
        const auto isect = ray::intersectPlaneForward(r, planePoint, planeNormal, 0.0);
        CHECK_FALSE(isect.hit);
        CHECK(isect.signedDistanceMetres == doctest::Approx(0.0));
        CHECK_FALSE(ray::intersectForwardPlane(r, planePoint, planeNormal, 0.0).has_value());
    }

    // Case 2: Ray origin lies on plane (t = 0.0) with default epsilon -> must be rejected
    {
        const auto r = ray::makeRay({0.0, 0.0, 0.0}, {0.0, 0.0, 1.0});
        const auto isect = ray::intersectPlaneForward(r, planePoint, planeNormal);
        CHECK_FALSE(isect.hit);
    }

    // Case 3: Ray origin behind plane pointing along normal (t < 0) with epsilon = 0.0 -> must be rejected
    {
        const auto r = ray::makeRay({0.0, 0.0, 0.5}, {0.0, 0.0, 1.0});
        const auto isect = ray::intersectPlaneForward(r, planePoint, planeNormal, 0.0);
        CHECK_FALSE(isect.hit);
        CHECK(isect.signedDistanceMetres < 0.0);
    }

    // Case 4: Ray origin in front of plane pointing towards plane (t > 0) with epsilon = 0.0 -> must hit
    {
        const auto r = ray::makeRay({0.0, 0.0, -0.5}, {0.0, 0.0, 1.0});
        const auto isect = ray::intersectPlaneForward(r, planePoint, planeNormal, 0.0);
        REQUIRE(isect.hit);
        CHECK(isect.signedDistanceMetres == doctest::Approx(0.5).epsilon(1e-12));
        CHECK(isect.pointMetres.z == doctest::Approx(0.0).epsilon(1e-12));
    }

    // Case 5: Ray distance exactly at epsilon threshold
    {
        constexpr double eps = 1e-6;
        const auto rAtEps = ray::makeRay({0.0, 0.0, -eps}, {0.0, 0.0, 1.0});
        const auto isectAtEps = ray::intersectPlaneForward(rAtEps, planePoint, planeNormal, eps);
        CHECK_FALSE(isectAtEps.hit); // t == eps is rejected (t <= epsilon)

        const auto rBeyondEps = ray::makeRay({0.0, 0.0, -(eps + 1e-8)}, {0.0, 0.0, 1.0});
        const auto isectBeyondEps = ray::intersectPlaneForward(rBeyondEps, planePoint, planeNormal, eps);
        CHECK(isectBeyondEps.hit);
    }

    // Case 6: Non-finite or negative epsilon throws invalid_argument
    {
        const auto r = ray::makeRay({0.0, 0.0, -0.5}, {0.0, 0.0, 1.0});
        CHECK_THROWS_AS((void)ray::intersectPlaneForward(r, planePoint, planeNormal, -1e-6), std::invalid_argument);
        CHECK_THROWS_AS((void)ray::intersectPlaneForward(r, planePoint, planeNormal, std::numeric_limits<double>::quiet_NaN()), std::invalid_argument);
        CHECK_THROWS_AS((void)ray::intersectPlaneForward(r, planePoint, planeNormal, std::numeric_limits<double>::infinity()), std::invalid_argument);
    }

    // Case 7: Invalid ray parameters throw invalid_argument
    {
        auto rBadWavelength = ray::makeRay({0.0, 0.0, -0.5}, {0.0, 0.0, 1.0});
        rBadWavelength.wavelengthMetres = 0.0;
        CHECK_THROWS_AS((void)ray::intersectPlaneForward(rBadWavelength, planePoint, planeNormal), std::invalid_argument);

        auto rBadPower = ray::makeRay({0.0, 0.0, -0.5}, {0.0, 0.0, 1.0});
        rBadPower.power = -0.5;
        CHECK_THROWS_AS((void)ray::intersectPlaneForward(rBadPower, planePoint, planeNormal), std::invalid_argument);
    }
}

TEST_CASE("planar aperture basis validation enforces finite, nonzero, normalized, and orthogonal vectors") {
    // Valid basis
    const ray::PlanarApertureBasis validBasis {
        .uAxis = {1.0, 0.0, 0.0},
        .vAxis = {0.0, 1.0, 0.0},
    };
    CHECK(ray::isPlanarApertureBasisValid(validBasis));
    CHECK_NOTHROW(ray::validatePlanarApertureBasis(validBasis));

    // Computed bases from various normals must all be valid orthonormal bases
    for (const auto& normal : {
             math::Vec3d {0.0, 0.0, 1.0},
             math::Vec3d {0.0, 0.0, -1.0},
             math::Vec3d {0.0, 1.0, 0.0},
             math::Vec3d {0.0, -1.0, 0.0},
             math::Vec3d {1.0, 0.0, 0.0},
             math::Vec3d {0.5773502691896257, 0.5773502691896257, 0.5773502691896257},
         }) {
        const auto computed = ray::computePlanarApertureBasis(normal);
        CHECK(ray::isPlanarApertureBasisValid(computed));
        CHECK_NOTHROW(ray::validatePlanarApertureBasis(computed));
        CHECK(math::dot(computed.uAxis, computed.vAxis) == doctest::Approx(0.0).epsilon(1e-12));
        CHECK(math::dot(computed.uAxis, math::normalized(normal)) == doctest::Approx(0.0).epsilon(1e-12));
        CHECK(math::dot(computed.vAxis, math::normalized(normal)) == doctest::Approx(0.0).epsilon(1e-12));
    }

    // Non-finite basis vector
    {
        ray::PlanarApertureBasis badBasis = validBasis;
        badBasis.uAxis.x = std::numeric_limits<double>::quiet_NaN();
        CHECK_FALSE(ray::isPlanarApertureBasisValid(badBasis));
        CHECK_THROWS_AS(ray::validatePlanarApertureBasis(badBasis), std::invalid_argument);
    }

    // Zero vector
    {
        ray::PlanarApertureBasis badBasis = validBasis;
        badBasis.uAxis = {0.0, 0.0, 0.0};
        CHECK_FALSE(ray::isPlanarApertureBasisValid(badBasis));
        CHECK_THROWS_AS(ray::validatePlanarApertureBasis(badBasis), std::invalid_argument);
    }

    // Non-normalized vector (length != 1.0)
    {
        ray::PlanarApertureBasis badBasis = validBasis;
        badBasis.uAxis = {2.0, 0.0, 0.0};
        CHECK_FALSE(ray::isPlanarApertureBasisValid(badBasis));
        CHECK_THROWS_AS(ray::validatePlanarApertureBasis(badBasis), std::invalid_argument);

        badBasis.uAxis = {0.5, 0.0, 0.0};
        CHECK_FALSE(ray::isPlanarApertureBasisValid(badBasis));
        CHECK_THROWS_AS(ray::validatePlanarApertureBasis(badBasis), std::invalid_argument);
    }

    // Non-orthogonal vectors (dot != 0.0)
    {
        ray::PlanarApertureBasis badBasis {
            .uAxis = {1.0, 0.0, 0.0},
            .vAxis = math::normalized(math::Vec3d {1.0, 1.0, 0.0}),
        };
        CHECK_FALSE(ray::isPlanarApertureBasisValid(badBasis));
        CHECK_THROWS_AS(ray::validatePlanarApertureBasis(badBasis), std::invalid_argument);
    }

    // isInsideRectangularAperture rejects invalid basis
    {
        ray::PlanarApertureBasis badBasis {
            .uAxis = {1.0, 0.0, 0.0},
            .vAxis = {1.0, 0.0, 0.0}, // parallel (non-orthogonal)
        };
        CHECK_THROWS_AS(
            (void)ray::isInsideRectangularAperture({0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, badBasis, 0.05, 0.05),
            std::invalid_argument);
    }
}

TEST_CASE("emitCollimatedRays provides strong exception safety and preserves caller buffer on error") {
    const auto validSource = scene::createDefaultCollimatedSource();

    // Pre-populate caller buffer with 3 distinct rays
    std::vector<ray::Ray> buffer = {
        ray::makeRay({1.0, 2.0, 3.0}, {0.0, 0.0, 1.0}, 500e-9, 0.1),
        ray::makeRay({4.0, 5.0, 6.0}, {0.0, 1.0, 0.0}, 600e-9, 0.2),
        ray::makeRay({7.0, 8.0, 9.0}, {1.0, 0.0, 0.0}, 700e-9, 0.3),
    };
    const auto snapshot = buffer;

    // 1. Failure on invalid source direction
    {
        auto badSource = validSource;
        badSource.direction = {0.0, 0.0, 0.0};
        CHECK_THROWS_AS(ray::emitCollimatedRays(badSource, 10, buffer), std::invalid_argument);
        REQUIRE(buffer.size() == snapshot.size());
        for (std::size_t i = 0; i < buffer.size(); ++i) {
            CHECK(buffer[i].originMetres.x == snapshot[i].originMetres.x);
            CHECK(buffer[i].wavelengthMetres == snapshot[i].wavelengthMetres);
            CHECK(buffer[i].power == snapshot[i].power);
        }
    }

    // 2. Failure on rayCount = 0
    {
        CHECK_THROWS_AS(ray::emitCollimatedRays(validSource, 0, buffer), std::invalid_argument);
        REQUIRE(buffer.size() == snapshot.size());
    }

    // 3. Failure on rayCount > kMaxCollimatedRayCount
    {
        CHECK_THROWS_AS(ray::emitCollimatedRays(validSource, ray::kMaxCollimatedRayCount + 1, buffer), std::invalid_argument);
        REQUIRE(buffer.size() == snapshot.size());
    }

    // 4. Failure on invalid pattern enum
    {
        CHECK_THROWS_AS(
            ray::emitCollimatedRays(validSource, 10, buffer, static_cast<ray::CollimatedRayPattern>(-5)),
            std::invalid_argument);
        REQUIRE(buffer.size() == snapshot.size());
    }

    // 5. Failure on negative beam radius
    {
        auto badSource = validSource;
        badSource.beamRadiusMetres = -0.01;
        CHECK_THROWS_AS(ray::emitCollimatedRays(badSource, 10, buffer), std::invalid_argument);
        REQUIRE(buffer.size() == snapshot.size());
    }
}

TEST_CASE("Collimated ray emission patterns strictly conserve power and satisfy geometric bounds for various sample counts") {
    scene::CollimatedSource source;
    source.originMetres = {0.05, -0.05, 0.10};
    source.direction = math::normalized(math::Vec3d {0.0, 0.6, 0.8});
    source.beamRadiusMetres = 0.025;
    source.wavelengthMetres = 532e-9;
    source.powerWatts = 7.5;

    const auto basis = ray::computePlanarApertureBasis(source.direction);

    // Test a wide variety of sample counts across all patterns
    const std::vector<std::size_t> testCounts = {1, 2, 3, 4, 7, 16, 25, 49, 100, 256, 1000};

    for (const auto pattern : {
             ray::CollimatedRayPattern::SingleRay,
             ray::CollimatedRayPattern::FibonacciDisk,
             ray::CollimatedRayPattern::ConcentricRings,
             ray::CollimatedRayPattern::UniformGrid,
         }) {
        for (const std::size_t count : testCounts) {
            std::vector<ray::Ray> buffer;
            ray::emitCollimatedRays(source, count, buffer, pattern);
            REQUIRE(buffer.size() == count);

            double sumPower = 0.0;
            for (const auto& r : buffer) {
                // Strict parallelism and normalization
                CHECK(r.direction.x == doctest::Approx(source.direction.x).epsilon(1e-12));
                CHECK(r.direction.y == doctest::Approx(source.direction.y).epsilon(1e-12));
                CHECK(r.direction.z == doctest::Approx(source.direction.z).epsilon(1e-12));
                CHECK(math::length(r.direction) == doctest::Approx(1.0).epsilon(1e-12));

                // Position within circular disk
                const math::Vec3d delta = r.originMetres - source.originMetres;
                const double u = math::dot(delta, basis.uAxis);
                const double v = math::dot(delta, basis.vAxis);
                const double dist = std::sqrt(u * u + v * v);
                CHECK(dist <= source.beamRadiusMetres + 1e-12);

                sumPower += r.power;
            }

            // Power conservation
            CHECK(sumPower == doctest::Approx(source.powerWatts).epsilon(1e-12));
        }
    }

    // Zero power source
    {
        auto zeroPowerSource = source;
        zeroPowerSource.powerWatts = 0.0;
        const auto rays = ray::emitCollimatedRays(zeroPowerSource, 50, ray::CollimatedRayPattern::UniformGrid);
        REQUIRE(rays.size() == 50);
        double totalP = 0.0;
        for (const auto& r : rays) {
            totalP += r.power;
        }
        CHECK(totalP == doctest::Approx(0.0));
    }
}
