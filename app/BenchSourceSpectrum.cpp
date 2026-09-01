#include "app/BenchSourceSpectrum.hpp"

#include <numeric>
#include <stdexcept>
#include <string>

namespace holobench::app {
namespace {

namespace bench = optics::scene;

struct SpectralIdentity final {
    double wavelengthMetres;
    const char* coherenceId;
};

[[nodiscard]] SpectralIdentity identityFor(
    BenchSourceSpectrumPreset preset) {
    switch (preset) {
    case BenchSourceSpectrumPreset::Red:
        return {638e-9, "red-recording"};
    case BenchSourceSpectrumPreset::Green:
        return {532e-9, "green-recording"};
    case BenchSourceSpectrumPreset::Blue:
        return {450e-9, "blue-recording"};
    case BenchSourceSpectrumPreset::Rgb:
        break;
    }
    throw std::invalid_argument(
        "RGB is a multi-channel laser preset, not one spectral identity");
}

[[nodiscard]] bench::SpectralChannel channelFor(
    BenchSourceSpectrumPreset preset,
    double powerWatts) {
    const auto identity = identityFor(preset);
    return {
        .wavelengthMetres = identity.wavelengthMetres,
        .powerWatts = powerWatts,
        .coherenceId = identity.coherenceId,
    };
}

} // namespace

bench::BenchComponent applySourceSpectrumPreset(
    const bench::BenchComponent& component,
    BenchSourceSpectrumPreset preset) {
    bench::validateBenchComponent(component);
    auto edited = component;
    if (component.kind == bench::BenchComponentKind::LaserSource) {
        auto parameters = std::get<bench::LaserSourceParameters>(
            component.parameters);
        const double totalPowerWatts = std::accumulate(
            parameters.channels.begin(),
            parameters.channels.end(),
            0.0,
            [](double total, const auto& channel) {
                return total + channel.powerWatts;
            });
        if (preset == BenchSourceSpectrumPreset::Rgb) {
            const double channelPowerWatts = totalPowerWatts / 3.0;
            parameters.channels = {
                channelFor(BenchSourceSpectrumPreset::Red, channelPowerWatts),
                channelFor(
                    BenchSourceSpectrumPreset::Green, channelPowerWatts),
                channelFor(BenchSourceSpectrumPreset::Blue, channelPowerWatts),
            };
        } else {
            parameters.channels = {channelFor(preset, totalPowerWatts)};
        }
        edited.parameters = std::move(parameters);
    } else if (component.kind
        == bench::BenchComponentKind::ObjectWavefrontSource) {
        if (preset == BenchSourceSpectrumPreset::Rgb) {
            throw std::invalid_argument(
                "an object source has one spectral channel; place red, green, and blue object sources separately");
        }
        auto parameters = std::get<bench::ObjectWavefrontSourceParameters>(
            component.parameters);
        parameters.channel = channelFor(
            preset, parameters.channel.powerWatts);
        edited.parameters = std::move(parameters);
    } else {
        throw std::invalid_argument(
            "spectral presets require a Laser Source or Object / Wavefront Source");
    }
    bench::validateBenchComponent(edited);
    return edited;
}

} // namespace holobench::app
