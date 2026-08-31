#include "app/UiFont.hpp"

#include <imgui.h>

#include <cstdio>
#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace holobench::app {
namespace {

constexpr char kLanguageSelectorText[] = "简体中文 (zh-Hans)";
constexpr char kWindowTitleText[]
    = "HoloBench — Optical Engineering Workbench";
constexpr char kSmokeText[] = "中文光学渲染检查";
constexpr ImWchar kPrintableLatinRanges[] {
    0x0020, 0x007E,
    0x00A0, 0x00FF,
    0,
};

[[nodiscard]] std::string missingGlyphMessage(
    std::string_view phase,
    unsigned int codepoint) {
    std::ostringstream message;
    message << "packaged UI font " << phase
            << " is missing required Unicode code point U+";
    message.setf(std::ios::uppercase);
    message << std::hex << codepoint;
    return message.str();
}

} // namespace

class UiFontAsset::Impl final {
public:
    Impl(
        std::filesystem::path fontPath,
        const lessons::LocalizationCatalog& localization)
        : fontPath_(std::move(fontPath)) {
        for (const auto locale : {
                 lessons::LessonLocale::English,
                 lessons::LessonLocale::SimplifiedChinese}) {
            for (const std::string_view message : localization.messages(locale)) {
                requiredTexts_.emplace_back(message);
            }
        }
        requiredTexts_.emplace_back(kLanguageSelectorText);
        requiredTexts_.emplace_back(kWindowTitleText);
        requiredTexts_.emplace_back(kSmokeText);
    }

    void install(ImGuiIO& io) {
        if (font_ != nullptr) {
            throw std::logic_error("packaged UI font is already installed");
        }
        if (!std::filesystem::is_regular_file(fontPath_)) {
            throw std::runtime_error(
                "packaged UI font file is missing: " + fontPath_.string());
        }
        const std::filesystem::path licensePath
            = fontPath_.parent_path() / "OFL.txt";
        if (!std::filesystem::is_regular_file(licensePath)) {
            throw std::runtime_error(
                "packaged UI font license is missing: "
                + licensePath.string());
        }

        glyphBuilder_.AddRanges(kPrintableLatinRanges);
        for (const std::string& text : requiredTexts_) {
            glyphBuilder_.AddText(text.c_str());
        }
        glyphBuilder_.BuildRanges(&glyphRanges_);

        ImFontConfig config;
        config.Flags |= ImFontFlags_NoLoadError;
        std::snprintf(
            config.Name, sizeof(config.Name), "%s", "Noto Sans CJK SC 18px");
        font_ = io.Fonts->AddFontFromFileTTF(
            fontPath_.string().c_str(), 18.0F, &config, glyphRanges_.Data);
        if (font_ == nullptr) {
            throw std::runtime_error(
                "packaged UI font could not be loaded: " + fontPath_.string());
        }
        io.FontDefault = font_;
        validateSourceCoverage();
    }

    void validateBakedCoverage(float sizePixels) const {
        if (font_ == nullptr) {
            throw std::logic_error("packaged UI font is not installed");
        }
        ImFontBaked* baked = font_->GetFontBaked(sizePixels);
        if (baked == nullptr) {
            throw std::runtime_error("packaged UI font was not baked");
        }
        forEachRequiredCodepoint([baked](unsigned int codepoint) {
            if (baked->FindGlyphNoFallback(
                    static_cast<ImWchar>(codepoint)) == nullptr) {
                throw std::runtime_error(
                    missingGlyphMessage("atlas", codepoint));
            }
        });
    }

    [[nodiscard]] std::size_t requiredGlyphCount() const noexcept {
        std::size_t count = 0U;
        forEachRequiredCodepoint([&count](unsigned int) { ++count; });
        return count;
    }

private:
    template <typename Visitor>
    void forEachRequiredCodepoint(Visitor&& visitor) const {
        const std::size_t bitCount
            = static_cast<std::size_t>(glyphBuilder_.UsedChars.Size) * 32U;
        for (std::size_t codepoint = 0U; codepoint < bitCount; ++codepoint) {
            if (glyphBuilder_.GetBit(codepoint)) {
                visitor(static_cast<unsigned int>(codepoint));
            }
        }
    }

    void validateSourceCoverage() const {
        forEachRequiredCodepoint([this](unsigned int codepoint) {
            if (!font_->IsGlyphInFont(static_cast<ImWchar>(codepoint))) {
                throw std::runtime_error(
                    missingGlyphMessage("source", codepoint));
            }
        });
    }

    std::filesystem::path fontPath_;
    std::vector<std::string> requiredTexts_;
    ImFontGlyphRangesBuilder glyphBuilder_;
    ImVector<ImWchar> glyphRanges_;
    ImFont* font_ = nullptr;
};

UiFontAsset::UiFontAsset(
    std::filesystem::path fontPath,
    const lessons::LocalizationCatalog& localization)
    : impl_(std::make_unique<Impl>(
          std::move(fontPath), localization)) {}

UiFontAsset::~UiFontAsset() = default;

void UiFontAsset::install(ImGuiIO& io) {
    impl_->install(io);
}

void UiFontAsset::validateBakedCoverage(float sizePixels) const {
    impl_->validateBakedCoverage(sizePixels);
}

std::size_t UiFontAsset::requiredGlyphCount() const noexcept {
    return impl_->requiredGlyphCount();
}

} // namespace holobench::app
