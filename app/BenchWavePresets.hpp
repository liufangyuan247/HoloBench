#pragma once

#include "app/BenchProject.hpp"

namespace holobench::app {

[[nodiscard]] BenchProject makeDoubleSlitExperimentPreset();
[[nodiscard]] BenchProject makeSingleSlitDiffractionPreset();
[[nodiscard]] BenchProject makeCircularDiffractionPreset();
[[nodiscard]] BenchProject makeMachZehnderInterferometerPreset();
[[nodiscard]] BenchProject makeGalileanBeamExpanderPreset();

} // namespace holobench::app
