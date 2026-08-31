#pragma once

#include "app/BenchProject.hpp"

namespace holobench::app {

// Ordinary editable unified-bench documents. These functions do not create a
// hidden workflow graph; callers may move, delete, duplicate, save, and reload
// every generated component through the same BenchProject path.
[[nodiscard]] BenchProject makeTransmissionHolographyPreset();
[[nodiscard]] BenchProject makeReflectionHolographyPreset();
[[nodiscard]] BenchProject makeRgbHolographyPreset();

} // namespace holobench::app
