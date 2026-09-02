#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace holobench::project {

inline constexpr std::uintmax_t kDefaultMaximumHashedAssetBytes
    = 64U * 1024U * 1024U;

[[nodiscard]] std::string sha256Hex(
    std::span<const std::byte> bytes);

[[nodiscard]] std::string sha256Hex(std::string_view text);

// Hashes the exact file bytes. The explicit size bound prevents an external
// calibration reference from turning project loading into unbounded I/O.
[[nodiscard]] std::string sha256FileHex(
    const std::filesystem::path& path,
    std::uintmax_t maximumBytes = kDefaultMaximumHashedAssetBytes);

} // namespace holobench::project
