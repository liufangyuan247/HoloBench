#pragma once

#include "optics/ray/SequentialLens.hpp"

namespace holobench::app {
// A meridional section of the actual rotational-surface prescription, in SI.
[[nodiscard]] std::vector<math::Vec3d>
lensSurfaceProfile(const optics::ray::PrescriptionSurface &surface, std::size_t segments = 128);
void validateLensProfileAssembly(const optics::ray::SequentialLensPrescription &prescription);
void setLensSurfaceEdgeSag(optics::ray::SequentialLensPrescription &prescription,
                           std::size_t surface, double sagMetres);
void setLensSurfaceSpacing(optics::ray::SequentialLensPrescription &prescription,
                           std::size_t surface, double spacingMetres);
void appendProfileLens(optics::ray::SequentialLensPrescription &prescription);
} // namespace holobench::app
