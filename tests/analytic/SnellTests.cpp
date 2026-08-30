#include <doctest/doctest.h>

#include <cmath>
#include <limits>
#include <numbers>

#include "optics/ray/Interface.hpp"

namespace math = holobench::math;
namespace ray = holobench::optics::ray;

namespace {

constexpr double kPi = std::numbers::pi_v<double>;

double degToRad(double deg) {
    return deg * (kPi / 180.0);
}

} // namespace

TEST_CASE("normal incidence passes straight through with Refracted status") {
    const math::Vec3d intersection {0.01, -0.02, 0.05};
    const math::Vec3d incidentDir {0.0, 0.0, 1.0};
    const auto incident = ray::makeRay({0.01, -0.02, 0.0}, incidentDir, 632.8e-9, 2.5);

    // Test with normal opposing incident ray (+Z vs -Z)
    for (const double nz : {-1.0, 1.0}) {
        const math::Vec3d normal {0.0, 0.0, nz};

        // Denser to rarer
        {
            const auto result = ray::interactInterface(incident, intersection, normal, 1.5, 1.0);
            REQUIRE(result.status == ray::InterfaceInteractionStatus::Refracted);
            CHECK(result.outgoingRay.originMetres.x == doctest::Approx(intersection.x).epsilon(1e-12));
            CHECK(result.outgoingRay.originMetres.y == doctest::Approx(intersection.y).epsilon(1e-12));
            CHECK(result.outgoingRay.originMetres.z == doctest::Approx(intersection.z).epsilon(1e-12));
            CHECK(result.outgoingRay.direction.x == doctest::Approx(0.0).epsilon(1e-12));
            CHECK(result.outgoingRay.direction.y == doctest::Approx(0.0).epsilon(1e-12));
            CHECK(result.outgoingRay.direction.z == doctest::Approx(1.0).epsilon(1e-12));
            CHECK(result.outgoingRay.power == doctest::Approx(incident.power).epsilon(1e-12));
        }

        // Rarer to denser
        {
            const auto result = ray::interactInterface(incident, intersection, normal, 1.0, 1.5);
            REQUIRE(result.status == ray::InterfaceInteractionStatus::Refracted);
            CHECK(result.outgoingRay.direction.x == doctest::Approx(0.0).epsilon(1e-12));
            CHECK(result.outgoingRay.direction.y == doctest::Approx(0.0).epsilon(1e-12));
            CHECK(result.outgoingRay.direction.z == doctest::Approx(1.0).epsilon(1e-12));
        }
    }
}

TEST_CASE("air to glass 30 degrees incidence satisfies Snell's law within 1e-12") {
    constexpr double nAir = 1.0;
    constexpr double nGlass = 1.5;
    constexpr double thetaI = kPi / 6.0; // 30 degrees

    // Incident direction in XZ plane hitting interface at Z=0 (normal along Z)
    const math::Vec3d dir {std::sin(thetaI), 0.0, std::cos(thetaI)};
    const auto incident = ray::makeRay({0.0, 0.0, -1.0}, dir);
    const math::Vec3d normal {0.0, 0.0, -1.0}; // Opposes incoming ray
    const math::Vec3d hitPoint {0.0, 0.0, 0.0};

    const auto result = ray::interactInterface(incident, hitPoint, normal, nAir, nGlass);
    REQUIRE(result.status == ray::InterfaceInteractionStatus::Refracted);

    // Analytic Snell's law: n_1 * sin(theta_1) = n_2 * sin(theta_2)
    // sin(theta_2) = (1.0 / 1.5) * sin(30 deg) = (2/3) * (1/2) = 1/3
    constexpr double expectedSinThetaT = 1.0 / 3.0;
    const double expectedCosThetaT = std::sqrt(1.0 - expectedSinThetaT * expectedSinThetaT); // sqrt(8/9) = 2*sqrt(2)/3
    const math::Vec3d expectedDir {expectedSinThetaT, 0.0, expectedCosThetaT};

    // Verify Snell law invariant error <= 1e-12
    const double actualSinThetaT = result.outgoingRay.direction.x;
    const double actualCosThetaT = result.outgoingRay.direction.z;
    const double snellLhs = nAir * std::sin(thetaI);
    const double snellRhs = nGlass * actualSinThetaT;
    CHECK(std::abs(snellLhs - snellRhs) <= 1e-12);

    // Verify transmitted direction components match analytic within 1e-12
    CHECK(std::abs(actualSinThetaT - expectedDir.x) <= 1e-12);
    CHECK(std::abs(actualCosThetaT - expectedDir.z) <= 1e-12);
    CHECK(std::abs(result.outgoingRay.direction.y - expectedDir.y) <= 1e-12);
    CHECK(std::abs(math::length(result.outgoingRay.direction) - 1.0) <= 1e-12);
}

TEST_CASE("glass to air critical angle boundary behavior (above, exact, and below)") {
    constexpr double nGlass = 1.5;
    constexpr double nAir = 1.0;
    const double thetaC = std::asin(nAir / nGlass); // Critical angle: asin(2/3) rad (~41.8103 deg)

    const math::Vec3d hitPoint {0.0, 0.0, 0.0};
    const math::Vec3d normal {0.0, 0.0, -1.0};

    // Subcase 1: Below critical angle (Refraction)
    for (const double delta : {degToRad(-10.0), degToRad(-1.0), -1e-4}) {
        const double thetaI = thetaC + delta;
        const math::Vec3d dir {std::sin(thetaI), 0.0, std::cos(thetaI)};
        const auto incident = ray::makeRay({0.0, 0.0, -1.0}, dir);

        const auto result = ray::interactInterface(incident, hitPoint, normal, nGlass, nAir);
        REQUIRE(result.status == ray::InterfaceInteractionStatus::Refracted);

        const double actualSinThetaT = result.outgoingRay.direction.x;
        const double snellLhs = nGlass * std::sin(thetaI);
        const double snellRhs = nAir * actualSinThetaT;
        CHECK(std::abs(snellLhs - snellRhs) <= 1e-12);
        CHECK(result.outgoingRay.direction.z > 0.0); // Continues into transmitted medium (+Z)
    }

    // Subcase 2: Exact critical angle yields tangential refraction (not spurious TIR)
    {
        const math::Vec3d dir {std::sin(thetaC), 0.0, std::cos(thetaC)};
        const auto incident = ray::makeRay({0.0, 0.0, -1.0}, dir);

        const auto result = ray::interactInterface(incident, hitPoint, normal, nGlass, nAir);
        REQUIRE(result.status == ray::InterfaceInteractionStatus::Refracted);

        // Outgoing direction should be purely tangential along the interface (+X, zero normal component)
        CHECK(std::abs(result.outgoingRay.direction.x - 1.0) <= 1e-12);
        CHECK(std::abs(result.outgoingRay.direction.y - 0.0) <= 1e-12);
        CHECK(std::abs(result.outgoingRay.direction.z - 0.0) <= 1e-12);
        CHECK(std::abs(math::length(result.outgoingRay.direction) - 1.0) <= 1e-12);
    }

    // Subcase 3: Above critical angle (Total Internal Reflection)
    for (const double delta : {1e-4, degToRad(1.0), degToRad(10.0)}) {
        const double thetaI = thetaC + delta;
        const math::Vec3d dir {std::sin(thetaI), 0.0, std::cos(thetaI)};
        const auto incident = ray::makeRay({0.0, 0.0, -1.0}, dir);

        const auto result = ray::interactInterface(incident, hitPoint, normal, nGlass, nAir);
        REQUIRE(result.status == ray::InterfaceInteractionStatus::TotalInternalReflection);

        // Law of reflection: reflected angle equals incident angle
        // Reflected direction is in XZ plane: x = sin(theta_i), z = -cos(theta_i) (reflects back to -Z)
        CHECK(std::abs(result.outgoingRay.direction.x - std::sin(thetaI)) <= 1e-12);
        CHECK(std::abs(result.outgoingRay.direction.z - (-std::cos(thetaI))) <= 1e-12);
        CHECK(std::abs(result.outgoingRay.direction.y - 0.0) <= 1e-12);
        CHECK(std::abs(math::length(result.outgoingRay.direction) - 1.0) <= 1e-12);
    }
}

TEST_CASE("total internal reflection satisfies law of reflection") {
    constexpr double nCore = 1.6;
    constexpr double nClad = 1.0;
    const math::Vec3d normal {0.0, 1.0, 0.0}; // Interface in XZ plane with normal along Y
    const math::Vec3d hitPoint {1.0, 0.0, 2.0};

    // Test several angles above the critical angle (critical angle is asin(1.0/1.6) ~= 38.68 deg)
    for (const double angleDeg : {45.0, 60.0, 75.0, 85.0}) {
        const double thetaI = degToRad(angleDeg);
        // Incident along Y (hitting surface at Y=0 from positive Y): dir = (cos(phi)*sin(theta), -cos(theta), sin(phi)*sin(theta))
        const double phi = degToRad(37.0); // Arbitrary 3D azimuthal angle
        const math::Vec3d dir {
            std::cos(phi) * std::sin(thetaI),
            -std::cos(thetaI),
            std::sin(phi) * std::sin(thetaI),
        };
        const auto incident = ray::makeRay({0.0, 1.0, 0.0}, dir);

        const auto result = ray::interactInterface(incident, hitPoint, normal, nCore, nClad);
        REQUIRE(result.status == ray::InterfaceInteractionStatus::TotalInternalReflection);

        // Angle of reflection: normal is +Y, incident normal component is -cos(theta_i), reflected normal component is +cos(theta_i)
        const math::Vec3d reflected = result.outgoingRay.direction;
        CHECK(std::abs(math::length(reflected) - 1.0) <= 1e-12);

        // Cosine of angle with interface normal
        const double cosReflectedAngle = math::dot(reflected, normal);
        CHECK(std::abs(cosReflectedAngle - std::cos(thetaI)) <= 1e-12);

        // Tangential component must be identical (in-plane law of reflection)
        CHECK(std::abs(reflected.x - dir.x) <= 1e-12);
        CHECK(std::abs(reflected.z - dir.z) <= 1e-12);
        CHECK(std::abs(reflected.y - (-dir.y)) <= 1e-12);
    }
}

TEST_CASE("surface normal flipping produces identical results") {
    const auto incident = ray::makeRay({0.0, 0.0, -1.0}, {0.3, 0.4, 0.8660254037844386});
    const math::Vec3d hitPoint {0.1, 0.2, 0.0};
    const math::Vec3d normalA {0.0, 0.0, 1.0};
    const math::Vec3d normalB {0.0, 0.0, -1.0};

    // Refraction case
    {
        const auto resA = ray::interactInterface(incident, hitPoint, normalA, 1.0, 1.5);
        const auto resB = ray::interactInterface(incident, hitPoint, normalB, 1.0, 1.5);
        CHECK(resA.status == resB.status);
        CHECK(resA.outgoingRay.direction.x == doctest::Approx(resB.outgoingRay.direction.x).epsilon(1e-15));
        CHECK(resA.outgoingRay.direction.y == doctest::Approx(resB.outgoingRay.direction.y).epsilon(1e-15));
        CHECK(resA.outgoingRay.direction.z == doctest::Approx(resB.outgoingRay.direction.z).epsilon(1e-15));
    }

    // TIR case
    {
        const auto resA = ray::interactInterface(incident, hitPoint, normalA, 1.5, 1.0);
        const auto resB = ray::interactInterface(incident, hitPoint, normalB, 1.5, 1.0);
        CHECK(resA.status == resB.status);
        CHECK(resA.outgoingRay.direction.x == doctest::Approx(resB.outgoingRay.direction.x).epsilon(1e-15));
        CHECK(resA.outgoingRay.direction.y == doctest::Approx(resB.outgoingRay.direction.y).epsilon(1e-15));
        CHECK(resA.outgoingRay.direction.z == doctest::Approx(resB.outgoingRay.direction.z).epsilon(1e-15));
    }
}

TEST_CASE("medium exchange and optical reversibility") {
    constexpr double n1 = 1.2;
    constexpr double n2 = 1.7;
    const math::Vec3d hitPoint {0.5, -0.3, 0.0};
    const math::Vec3d normal {0.0, 0.0, -1.0};

    // Forward ray from medium 1 to medium 2
    const double thetaI = degToRad(35.0);
    const math::Vec3d fwdDir {std::sin(thetaI), 0.0, std::cos(thetaI)};
    const auto fwdIncident = ray::makeRay({0.0, 0.0, -1.0}, fwdDir);
    const auto fwdResult = ray::interactInterface(fwdIncident, hitPoint, normal, n1, n2);
    REQUIRE(fwdResult.status == ray::InterfaceInteractionStatus::Refracted);

    // Reversed ray traveling backwards along transmitted direction from medium 2 to medium 1
    const math::Vec3d revDir = -fwdResult.outgoingRay.direction;
    const auto revIncident = ray::makeRay({0.0, 0.0, 1.0}, revDir);
    const auto revResult = ray::interactInterface(revIncident, hitPoint, normal, n2, n1);
    REQUIRE(revResult.status == ray::InterfaceInteractionStatus::Refracted);

    // Outgoing direction should exactly oppose original forward direction
    const math::Vec3d expectedRevOut = -fwdDir;
    CHECK(std::abs(revResult.outgoingRay.direction.x - expectedRevOut.x) <= 1e-12);
    CHECK(std::abs(revResult.outgoingRay.direction.y - expectedRevOut.y) <= 1e-12);
    CHECK(std::abs(revResult.outgoingRay.direction.z - expectedRevOut.z) <= 1e-12);
}

TEST_CASE("illegal refractive indices and invalid geometries throw invalid_argument") {
    const auto validRay = ray::makeRay({0.0, 0.0, 0.0}, {0.0, 0.0, 1.0});
    const math::Vec3d validHit {0.0, 0.0, 1.0};
    const math::Vec3d validNormal {0.0, 0.0, 1.0};

    // Non-positive or non-finite incident refractive index
    CHECK_THROWS_AS((void)ray::interactInterface(validRay, validHit, validNormal, 0.0, 1.5), std::invalid_argument);
    CHECK_THROWS_AS((void)ray::interactInterface(validRay, validHit, validNormal, -1.0, 1.5), std::invalid_argument);
    CHECK_THROWS_AS((void)ray::interactInterface(validRay, validHit, validNormal, std::numeric_limits<double>::quiet_NaN(), 1.5), std::invalid_argument);
    CHECK_THROWS_AS((void)ray::interactInterface(validRay, validHit, validNormal, std::numeric_limits<double>::infinity(), 1.5), std::invalid_argument);

    // Non-positive or non-finite transmitted refractive index
    CHECK_THROWS_AS((void)ray::interactInterface(validRay, validHit, validNormal, 1.0, 0.0), std::invalid_argument);
    CHECK_THROWS_AS((void)ray::interactInterface(validRay, validHit, validNormal, 1.0, -1.5), std::invalid_argument);
    CHECK_THROWS_AS((void)ray::interactInterface(validRay, validHit, validNormal, 1.0, std::numeric_limits<double>::quiet_NaN()), std::invalid_argument);
    CHECK_THROWS_AS((void)ray::interactInterface(validRay, validHit, validNormal, 1.0, std::numeric_limits<double>::infinity()), std::invalid_argument);

    // Zero or non-finite normal
    CHECK_THROWS_AS((void)ray::interactInterface(validRay, validHit, {0.0, 0.0, 0.0}, 1.0, 1.5), std::invalid_argument);
    CHECK_THROWS_AS((void)ray::interactInterface(validRay, validHit, {std::numeric_limits<double>::quiet_NaN(), 0.0, 1.0}, 1.0, 1.5), std::invalid_argument);

    // Non-finite intersection
    CHECK_THROWS_AS((void)ray::interactInterface(validRay, {std::numeric_limits<double>::infinity(), 0.0, 0.0}, validNormal, 1.0, 1.5), std::invalid_argument);
}

