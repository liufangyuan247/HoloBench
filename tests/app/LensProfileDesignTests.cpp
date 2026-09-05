#include "app/ChimeraRecipe.hpp"
#include "app/LensProfileDesign.hpp"
#include "optics/io/LensPrescriptionIO.hpp"
#include "optics/ray/LensPrescriptionCatalog.hpp"
#include "optics/scene/InstrumentDimensions.hpp"
#include <cmath>
#include <doctest/doctest.h>

TEST_CASE("2D lens section agrees with an independent circle sag and edits the true prescription") {
    auto p = holobench::optics::ray::makeDefaultNBk7BiconvexPrescription();
    const auto points = holobench::app::lensSurfaceProfile(p.surfaces.front());
    for (const auto &point : points)
        CHECK(point.z ==
              doctest::Approx(0.05 - std::sqrt(0.05 * 0.05 - point.y * point.y)).epsilon(1e-10));
    holobench::app::setLensSurfaceEdgeSag(p, 0, 0.002);
    CHECK(p.surfaces[0].geometry.curvaturePerMetre ==
          doctest::Approx(2 * 0.002 / (0.01 * 0.01 + 0.002 * 0.002)));
    const auto old = p;
    CHECK_THROWS(holobench::app::setLensSurfaceEdgeSag(p, 0, 0.02));
    CHECK(p == old);
}
TEST_CASE("2D lens groups retain air gaps and reject intersecting surfaces transactionally") {
    auto p = holobench::optics::ray::makeDefaultNBk7BiconvexPrescription();
    holobench::app::appendProfileLens(p);
    REQUIRE(p.surfaces.size() == 4U);
    const double thickness = p.surfaces[3].localToWorld.translationMetres.z -
                             p.surfaces[2].localToWorld.translationMetres.z;
    holobench::app::setLensSurfaceSpacing(p, 2, 0.02);
    CHECK(p.surfaces[2].localToWorld.translationMetres.z -
              p.surfaces[1].localToWorld.translationMetres.z ==
          doctest::Approx(0.02));
    CHECK(p.surfaces[3].localToWorld.translationMetres.z -
              p.surfaces[2].localToWorld.translationMetres.z ==
          doctest::Approx(thickness));
    const auto before = p;
    CHECK_THROWS(holobench::app::setLensSurfaceSpacing(p, 1, 1e-5));
    CHECK(p == before);
    const auto restored = holobench::optics::io::parseLensPrescriptionJson(
        holobench::optics::io::serializeLensPrescriptionJson(p));
    CHECK(restored == p);
    const auto trace = holobench::optics::ray::traceSequentialLens(
        holobench::optics::ray::makeRay({0, 0.001, -0.02}, {0, 0, 1}), restored, {});
    CHECK(trace.status == holobench::optics::ray::SequentialTraceStatus::Completed);
    CHECK(trace.records.size() == 4U);
}
TEST_CASE("CHIMERA mounted bases sit above the table with unchanged optical separations") {
    const auto compiled = holobench::app::chimera::compileChimeraRecipe(
        holobench::app::chimera::makeCanonicalChimeraRecipe());
    CHECK(compiled.feasible());
    CHECK(holobench::optics::scene::findBaseInterferences(compiled.project.scene).empty());
    for (const auto &component : compiled.project.scene.components()) {
        CHECK(component.transform.translationMetres.y == doctest::Approx(0.1));
        if (component.kind == holobench::optics::scene::BenchComponentKind::FieldProbe)
            continue;
        REQUIRE(component.mechanicalAssembly);
        CHECK(component.mechanicalAssembly->benchFrame.translationMetres.y ==
              doctest::Approx(0.005));
    }
}
TEST_CASE("oriented base overlap check distinguishes contact from penetration") {
    namespace s = holobench::optics::scene;
    auto a = s::makeDefaultBenchComponent(s::BenchComponentKind::PlanarMirror, "a");
    s::applyMechanicalAssembly(a, s::makeDefaultMechanicalAssembly(a));
    auto b = a;
    b.id = "b";
    CHECK(s::instrumentBasesOverlap(a, b));
    auto pose = b.transform;
    pose.translationMetres.x += s::instrumentBaseDimensions(a).x;
    s::rebaseMechanicalAssembly(b, pose);
    CHECK_FALSE(s::instrumentBasesOverlap(a, b));
    pose.translationMetres.x -= 0.001;
    s::rebaseMechanicalAssembly(b, pose);
    CHECK(s::instrumentBasesOverlap(a, b));
}
