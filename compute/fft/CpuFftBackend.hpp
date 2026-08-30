#pragma once

#include <complex>
#include <cstddef>
#include <span>
#include <vector>

#include "compute/fft/IFftBackend.hpp"

namespace holobench::compute::fft {

class CpuFftBackend final : public IFftBackend {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "CPU radix-2 reference"; }
    [[nodiscard]] bool supportsDimensions(std::size_t width, std::size_t height) const noexcept override;
    void forward2D(field::ComplexField2D& field) override;
    void inverse2D(field::ComplexField2D& field) override;

    [[nodiscard]] std::size_t scratchCapacitySamples() const noexcept { return scratch_.capacity(); }

private:
    void transform2D(field::ComplexField2D& field, bool inverse);
    static void transform1D(std::span<std::complex<double>> values, bool inverse) noexcept;

    std::vector<std::complex<double>> scratch_;
};

} // namespace holobench::compute::fft
