#pragma once

#include <cstddef>

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

class AngularSpectrumPropagator final {
public:
    explicit AngularSpectrumPropagator(fft::IFftBackend& fftBackend) noexcept;

    AngularSpectrumDiagnostics propagateInPlace(
        field::ComplexField2D& field,
        double distanceMetres);

private:
    fft::IFftBackend& fftBackend_;
};

} // namespace holobench::compute::propagation
