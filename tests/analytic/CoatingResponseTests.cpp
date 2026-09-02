#include <doctest/doctest.h>

#include <stdexcept>
#include <string>

#include "optics/material/CoatingResponse.hpp"

namespace material = holobench::optics::material;

namespace {

material::CalibratedCoatingResponse makeResponse() {
    return {
        "measured-scalar-coating",
        {450e-9, 650e-9},
        {0.0, 1.0},
        {
            {.powerReflectivity = 0.20, .powerTransmissivity = 0.70},
            {.powerReflectivity = 0.40, .powerTransmissivity = 0.50},
            {.powerReflectivity = 0.60, .powerTransmissivity = 0.30},
            {.powerReflectivity = 0.80, .powerTransmissivity = 0.10},
        },
    };
}

} // namespace

TEST_CASE("coating response bilinearly interpolates passive measured power") {
    const auto response = makeResponse();
    const auto exact = response.evaluate(450e-9, 0.0);
    CHECK(exact.calibrationId == "measured-scalar-coating");
    CHECK(exact.power.powerReflectivity == doctest::Approx(0.20));
    CHECK(exact.power.powerTransmissivity == doctest::Approx(0.70));
    CHECK(exact.power.powerAbsorptivity() == doctest::Approx(0.10));

    const auto centre = response.evaluate(550e-9, 0.5);
    CHECK(centre.power.powerReflectivity == doctest::Approx(0.50));
    CHECK(centre.power.powerTransmissivity == doctest::Approx(0.40));
    CHECK(centre.power.powerAbsorptivity() == doctest::Approx(0.10));
    CHECK_THROWS_AS(
        static_cast<void>(response.evaluate(700e-9, 0.5)),
        std::out_of_range);
    CHECK_THROWS_AS(
        static_cast<void>(response.evaluate(550e-9, 1.1)),
        std::out_of_range);
}

TEST_CASE("coating response JSON is canonical strict and energy conserving") {
    const auto response = makeResponse();
    const std::string encoded = material::serializeCoatingResponseJson(response);
    const auto restored = material::deserializeCoatingResponseJson(encoded);
    CHECK(restored.calibrationId() == response.calibrationId());
    CHECK(restored.vacuumWavelengthsMetres()
        == response.vacuumWavelengthsMetres());
    CHECK(restored.incidenceAnglesRadians()
        == response.incidenceAnglesRadians());
    CHECK(restored.cells() == response.cells());
    CHECK(material::serializeCoatingResponseJson(restored) == encoded);

    std::string unknownKey = encoded;
    unknownKey.replace(
        unknownKey.find("\"model\""),
        std::string("\"model\"").size(),
        "\"unknown\"");
    CHECK_THROWS_AS(
        static_cast<void>(
            material::deserializeCoatingResponseJson(unknownKey)),
        std::invalid_argument);
    CHECK_THROWS_AS(
        material::CalibratedCoatingResponse(
            "active-coating",
            {450e-9, 650e-9},
            {0.0, 1.0},
            {
                {.powerReflectivity = 0.8, .powerTransmissivity = 0.3},
                {}, {}, {},
            }),
        std::invalid_argument);
}
