#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace holobench::app::lessons {

enum class LessonLocale {
    English,
    SimplifiedChinese,
};

struct LocalizedMessage final {
    LessonLocale locale = LessonLocale::English;
    std::string key;
    std::string text;
};

class LocalizationCatalog final {
public:
    explicit LocalizationCatalog(std::vector<LocalizedMessage> messages);

    [[nodiscard]] bool contains(
        LessonLocale locale,
        std::string_view key) const noexcept;
    [[nodiscard]] const std::string& text(
        LessonLocale locale,
        std::string_view key) const;
    [[nodiscard]] std::vector<std::string_view> messages(
        LessonLocale locale) const;

private:
    std::map<std::pair<LessonLocale, std::string>, std::string> messages_;
};

[[nodiscard]] std::string_view lessonLocaleCode(LessonLocale locale) noexcept;
[[nodiscard]] LocalizationCatalog makeDefaultLessonLocalization();

} // namespace holobench::app::lessons
