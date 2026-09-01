#pragma once

#include "optics/scene/BenchScene.hpp"

namespace holobench::app {

enum class BenchSourceSpectrumPreset {
    Red,
    Green,
    Blue,
    Rgb,
};

// Returns an ordinary edited source component. Single-colour presets preserve
// source power; RGB splits a laser's existing total power equally across three
// independent wavelength/coherence channels. Object sources deliberately
// remain one spectral channel per component.
[[nodiscard]] optics::scene::BenchComponent applySourceSpectrumPreset(
    const optics::scene::BenchComponent& component,
    BenchSourceSpectrumPreset preset);

} // namespace holobench::app
