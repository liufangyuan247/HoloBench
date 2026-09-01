#include <doctest/doctest.h>

#include <numeric>

#include "app/BenchSourceSpectrum.hpp"

namespace app = holobench::app;
namespace scene = holobench::optics::scene;

TEST_CASE("RGB laser preset preserves total power and independent identities") {
    auto laser = scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::LaserSource, "laser");
    auto parameters = std::get<scene::LaserSourceParameters>(laser.parameters);
    parameters.channels.front().powerWatts = 0.9;
    laser.parameters = parameters;

    const auto rgb = app::applySourceSpectrumPreset(
        laser, app::BenchSourceSpectrumPreset::Rgb);
    const auto& channels = std::get<scene::LaserSourceParameters>(
        rgb.parameters).channels;
    REQUIRE(channels.size() == 3U);
    CHECK(channels[0].wavelengthMetres == doctest::Approx(638e-9));
    CHECK(channels[1].wavelengthMetres == doctest::Approx(532e-9));
    CHECK(channels[2].wavelengthMetres == doctest::Approx(450e-9));
    CHECK(channels[0].coherenceId == "red-recording");
    CHECK(channels[1].coherenceId == "green-recording");
    CHECK(channels[2].coherenceId == "blue-recording");
    CHECK(std::accumulate(
        channels.begin(), channels.end(), 0.0,
        [](double total, const auto& channel) {
            return total + channel.powerWatts;
        }) == doctest::Approx(0.9));
}

TEST_CASE("single-colour source presets preserve power and reject invalid use") {
    auto object = scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::ObjectWavefrontSource, "object");
    auto parameters = std::get<scene::ObjectWavefrontSourceParameters>(
        object.parameters);
    parameters.channel.powerWatts = 0.25;
    object.parameters = parameters;

    const auto blue = app::applySourceSpectrumPreset(
        object, app::BenchSourceSpectrumPreset::Blue);
    const auto& channel = std::get<scene::ObjectWavefrontSourceParameters>(
        blue.parameters).channel;
    CHECK(channel.wavelengthMetres == doctest::Approx(450e-9));
    CHECK(channel.powerWatts == doctest::Approx(0.25));
    CHECK(channel.coherenceId == "blue-recording");
    CHECK_THROWS_AS(
        static_cast<void>(app::applySourceSpectrumPreset(
            object, app::BenchSourceSpectrumPreset::Rgb)),
        std::invalid_argument);

    const auto plate = scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::HolographicPlate, "plate");
    CHECK_THROWS_AS(
        static_cast<void>(app::applySourceSpectrumPreset(
            plate, app::BenchSourceSpectrumPreset::Green)),
        std::invalid_argument);
}
