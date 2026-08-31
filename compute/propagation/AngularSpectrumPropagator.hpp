#pragma once

#include <complex>
#include <cstddef>
#include <vector>

namespace holobench::field {
class ComplexField2D;
}

namespace holobench::compute::fft {
class IFftBackend;
}

namespace holobench::compute::propagation {

struct AngularSpectrumDiagnostics final {
    std::size_t propagatingBinCount = 0;
    std::size_t evanescentBinCount = 0;
};

struct AngularSpectrumTransferFunction final {
    std::vector<std::complex<double>> samples;
    AngularSpectrumDiagnostics diagnostics;
};

[[nodiscard]] AngularSpectrumTransferFunction makeAngularSpectrumTransferFunction(
    const field::ComplexField2D& field,
    double distanceMetres);

class AngularSpectrumPropagator final {
public:
    explicit AngularSpectrumPropagator(fft::IFftBackend& fftBackend) noexcept;

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

    [[nodiscard]] AngularSpectrumDiagnostics ensureTransferFunction(
        const field::ComplexField2D& field,
        double distanceMetres);

    fft::IFftBackend& fftBackend_;
    TransferFunctionKey cachedKey_;
    bool hasCachedTransferFunction_ = false;
    std::vector<std::complex<double>> transferFunction_;
    AngularSpectrumDiagnostics cachedDiagnostics_;
};

} // namespace holobench::compute::propagation
