#pragma once

#include <filesystem>
#include <cstddef>
#include <string>
#include <string_view>

#include "optics/slm/SlmResponse.hpp"

namespace holobench::optics::slm {

inline constexpr int kSlmResponseFormatVersion = 1;
inline constexpr std::size_t kMaximumSlmResponseJsonBytes
    = 16U * 1024U * 1024U;

[[nodiscard]] std::string serializeSlmResponseJson(
    const CalibratedSlmResponse& response);

[[nodiscard]] CalibratedSlmResponse deserializeSlmResponseJson(
    std::string_view jsonText);

void saveSlmResponseJson(
    const std::filesystem::path& path,
    const CalibratedSlmResponse& response);

[[nodiscard]] CalibratedSlmResponse loadSlmResponseJson(
    const std::filesystem::path& path);

} // namespace holobench::optics::slm
