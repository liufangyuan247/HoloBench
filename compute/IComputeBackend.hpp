#pragma once

#include <string_view>

namespace holobench::compute {

enum class BackendKind {
    CpuReference,
    OpenGlCompute,
    Cuda,
};

struct BackendCapabilities final {
    bool supportsComplexFields = false;
    bool supportsFft2D = false;
    bool supportsRayBatches = false;
};

class IComputeBackend {
public:
    virtual ~IComputeBackend() = default;
    [[nodiscard]] virtual BackendKind kind() const noexcept = 0;
    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
    [[nodiscard]] virtual BackendCapabilities capabilities() const noexcept = 0;
};

} // namespace holobench::compute

