#pragma once

#include <cstdint>

namespace holobench::render::gl {

void installDebugCallback();
[[nodiscard]] std::uint32_t errorCount() noexcept;

} // namespace holobench::render::gl
