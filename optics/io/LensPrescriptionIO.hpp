#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include "optics/ray/SequentialLens.hpp"

namespace holobench::optics::io {

inline constexpr int kLensPrescriptionJsonFormatVersion = 1;
inline constexpr int kLensPrescriptionCsvFormatVersion = 1;

[[nodiscard]] std::string serializeLensPrescriptionJson(
    const ray::SequentialLensPrescription &prescription);

[[nodiscard]] ray::SequentialLensPrescription
parseLensPrescriptionJson(std::string_view encoded);

void saveLensPrescriptionJson(
    const ray::SequentialLensPrescription &prescription,
    const std::filesystem::path &path);

[[nodiscard]] ray::SequentialLensPrescription
loadLensPrescriptionJson(const std::filesystem::path &path);

[[nodiscard]] std::string serializeLensPrescriptionCsv(
    const ray::SequentialLensPrescription &prescription);

[[nodiscard]] ray::SequentialLensPrescription
parseLensPrescriptionCsv(std::string_view encoded);

void saveLensPrescriptionCsv(
    const ray::SequentialLensPrescription &prescription,
    const std::filesystem::path &path);

[[nodiscard]] ray::SequentialLensPrescription
loadLensPrescriptionCsv(const std::filesystem::path &path);

} // namespace holobench::optics::io
