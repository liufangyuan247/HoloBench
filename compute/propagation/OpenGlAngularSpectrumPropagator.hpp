#pragma once

#include <complex>
#include <cstddef>
#include <vector>

#include "compute/propagation/AngularSpectrumPropagator.hpp"

namespace holobench::field {
class ComplexField2D;
}

namespace holobench::compute::fft {
class OpenGlComputeFftBackend;
}

namespace holobench::compute::propagation {

// Interactive fused OpenGL path: upload -> FFT -> ASM transfer -> inverse FFT
// -> download. There is no CPU fallback; the CPU reference propagator remains
// the validation oracle.
class OpenGlAngularSpectrumPropagator final {
public:
    explicit OpenGlAngularSpectrumPropagator(fft::OpenGlComputeFftBackend& backend) noexcept;

    AngularSpectrumDiagnostics propagateInPlace(
        field::ComplexField2D& field,
        double distanceMetres);

private:
    struct TransferFunctionKey final {
        std::size_t width = 0;
        std::size_t height = 0;
        double pitchXMetres = 0.0;
        double pitchYMetres = 0.0;
        double vacuumWavelengthMetres = 0.0;
        double refractiveIndex = 0.0;
        double distanceMetres = 0.0;

        [[nodiscard]] bool operator==(const TransferFunctionKey&) const noexcept = default;
    };

    fft::OpenGlComputeFftBackend& backend_;
    TransferFunctionKey cachedKey_;
    bool hasCachedTransferFunction_ = false;
    std::vector<std::complex<double>> transferFunction_;
    AngularSpectrumDiagnostics cachedDiagnostics_;
};

} // namespace holobench::compute::propagation
