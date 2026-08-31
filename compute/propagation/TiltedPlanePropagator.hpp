#pragma once

#include <cstddef>

#include "core/math/RigidTransform.hpp"

namespace holobench::field {
class ComplexField2D;
}

namespace holobench::compute::fft {
class IFftBackend;
}

namespace holobench::compute::propagation {

struct TiltedPlaneDiagnostics final {
    std::size_t propagatingOutputBinCount = 0;
    std::size_t evanescentOutputBinCount = 0;
    std::size_t sourceBandRejectedBinCount = 0;
    std::size_t oppositeHemisphereBinCount = 0;
    std::size_t interpolatedOutputBinCount = 0;
};

// Rotates a scalar angular spectrum between arbitrary physical plane frames.
// Both grids retain the input dimensions and pitch. The preferred propagation
// direction selects the physical Helmholtz hemisphere; spectral content that
// cannot be represented on either regular grid is explicitly zeroed and
// counted. The padded overload embeds the requested window in a centred 2x
// grid before rotation and crops the output window.
class TiltedPlanePropagator final {
public:
    explicit TiltedPlanePropagator(fft::IFftBackend& fftBackend) noexcept;

    TiltedPlaneDiagnostics propagateInPlace(
        field::ComplexField2D& field,
        const math::RigidTransform3d& inputPlaneLocalToWorld,
        const math::RigidTransform3d& outputPlaneLocalToWorld,
        math::Vec3d preferredPropagationDirectionWorld);

    TiltedPlaneDiagnostics propagatePaddedInPlace(
        field::ComplexField2D& field,
        const math::RigidTransform3d& inputPlaneLocalToWorld,
        const math::RigidTransform3d& outputPlaneLocalToWorld,
        math::Vec3d preferredPropagationDirectionWorld);

private:
    fft::IFftBackend& fftBackend_;
};

} // namespace holobench::compute::propagation
