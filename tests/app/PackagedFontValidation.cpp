#include <imgui.h>

#include <cstdio>
#include <exception>
#include <filesystem>
#include <stdexcept>

#include "app/UiFont.hpp"
#include "app/lessons/Localization.hpp"

#ifndef HOLOBENCH_PACKAGED_FONT_FILE
#error "HOLOBENCH_PACKAGED_FONT_FILE must identify the packaged UI font"
#endif

int main() {
    try {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        const auto localization
            = holobench::app::lessons::makeDefaultLessonLocalization();
        holobench::app::UiFontAsset font(
            std::filesystem::path(HOLOBENCH_PACKAGED_FONT_FILE),
            localization);
        font.install(ImGui::GetIO());
        if (!ImGui::GetIO().Fonts->Build()) {
            throw std::runtime_error("packaged UI font atlas build failed");
        }
        font.validateBakedCoverage();
        std::printf(
            "font=NotoSansCJKsc-Regular.otf required_glyphs=%zu "
            "english=true zh_hans=true atlas=true\n",
            font.requiredGlyphCount());
        ImGui::DestroyContext();
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Packaged font validation failed: %s\n", error.what());
        if (ImGui::GetCurrentContext() != nullptr) {
            ImGui::DestroyContext();
        }
        return 1;
    }
}
