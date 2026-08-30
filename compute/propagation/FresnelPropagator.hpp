#pragma once

#include <cstddef>

namespace holobench::field {
class ComplexField2D;
}

namespace holobench::compute::fft {
class IFftBackend;
}

namespace holobench::compute::propagation {

struct FresnelDiagnostics final {
    std::size_t propagatedBinCount = 0;
};

class FresnelPropagator final {
public:
    explicit FresnelPropagator(fft::IFftBackend& fftBackend) noexcept;

    FresnelDiagnostics propagateInPlace(
        field::ComplexField2D& field,
        double distanceMetres);

private:
    fft::IFftBackend& fftBackend_;
};

using FresnelTransferFunctionPropagator = FresnelPropagator;

} // namespace holobench::compute::propagation
