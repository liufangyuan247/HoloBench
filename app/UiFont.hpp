#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>

#include "app/lessons/Localization.hpp"

struct ImGuiIO;

namespace holobench::app {

class UiFontAsset final {
public:
    UiFontAsset(
        std::filesystem::path fontPath,
        const lessons::LocalizationCatalog& localization);
    ~UiFontAsset();

    UiFontAsset(const UiFontAsset&) = delete;
    UiFontAsset& operator=(const UiFontAsset&) = delete;

    void install(ImGuiIO& io);
    void validateBakedCoverage(float sizePixels = 18.0F) const;

    [[nodiscard]] std::size_t requiredGlyphCount() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace holobench::app
