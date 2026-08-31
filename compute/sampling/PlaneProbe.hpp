#pragma once

#include <complex>
#include <cstddef>
#include <span>
#include <vector>

namespace holobench::field {
class ComplexField2D;
}

namespace holobench::compute::fft {
class IFftBackend;
}

namespace holobench::compute::sampling {

struct PlaneProbeSample final {
    double distanceMetres = 0.0;
    std::complex<double> fieldValue {0.0, 0.0};
    double intensity = 0.0;
    double wrappedPhaseRadians = 0.0;
    bool phaseValid = false;
    std::size_t propagatingBinCount = 0U;
    std::size_t evanescentBinCount = 0U;
};

struct PlaneProbeResult final {
    std::size_t xIndex = 0U;
    std::size_t yIndex = 0U;
    double xCoordinateMetres = 0.0;
    double yCoordinateMetres = 0.0;
    std::vector<PlaneProbeSample> samples;
};

/** Samples one fixed transverse grid location across arbitrary ASM z planes. */
[[nodiscard]] PlaneProbeResult probeAngularSpectrumPlanes(
    const field::ComplexField2D& sourcePlane,
    std::size_t xIndex,
    std::size_t yIndex,
    std::span<const double> distancesMetres,
    fft::IFftBackend& fftBackend);

} // namespace holobench::compute::sampling
