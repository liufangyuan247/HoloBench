#pragma once

#include "app/BenchProject.hpp"

namespace holobench::app {

[[nodiscard]] BenchProject makeDoubleSlitExperimentPreset();
[[nodiscard]] BenchProject makeSingleSlitDiffractionPreset();
[[nodiscard]] BenchProject makeCircularDiffractionPreset();

} // namespace holobench::app
