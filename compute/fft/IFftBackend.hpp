#pragma once

#include <cstddef>
#include <string_view>

namespace holobench::field {
class ComplexField2D;
}

namespace holobench::compute::fft {

class IFftBackend {
public:
    virtual ~IFftBackend() = default;

    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
    [[nodiscard]] virtual bool supportsDimensions(std::size_t width, std::size_t height) const noexcept = 0;
    virtual void forward2D(field::ComplexField2D& field) = 0;
    virtual void inverse2D(field::ComplexField2D& field) = 0;
};

} // namespace holobench::compute::fft
