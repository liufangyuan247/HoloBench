#pragma once
#include "app/LensProfileDesign.hpp"
#include <string>
namespace holobench::app {
// Returns true only for an accepted prescription mutation.
bool drawLensProfileEditor(optics::ray::SequentialLensPrescription &prescription,
                           std::size_t &selectedSurface, std::string &diagnostic);
} // namespace holobench::app
