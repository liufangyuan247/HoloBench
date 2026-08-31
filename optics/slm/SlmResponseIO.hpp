#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include "optics/slm/SlmResponse.hpp"

namespace holobench::optics::slm {

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
