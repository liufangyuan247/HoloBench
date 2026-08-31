#include <doctest/doctest.h>

#include <ostream>
#include <stdexcept>

#include "app/lessons/LessonCatalog.hpp"
#include "app/lessons/Localization.hpp"

namespace lessons = holobench::app::lessons;

TEST_SUITE("LessonLocalization") {

TEST_CASE("default localization covers catalog identity in English and Chinese") {
    const auto catalog = lessons::makeDefaultLessonCatalog();
    const auto localization = lessons::makeDefaultLessonLocalization();
    for (const auto& definition : catalog.lessons()) {
        for (const auto locale : {
                 lessons::LessonLocale::English,
                 lessons::LessonLocale::SimplifiedChinese}) {
            CHECK(localization.contains(locale, definition.titleKey));
            CHECK(localization.contains(locale, definition.objectiveKey));
            CHECK_FALSE(localization.text(locale, definition.titleKey).empty());
        }
    }
    CHECK(lessons::lessonLocaleCode(lessons::LessonLocale::English) == "en");
    CHECK(lessons::lessonLocaleCode(lessons::LessonLocale::SimplifiedChinese)
        == "zh-Hans");
}

TEST_CASE("implemented workflows localize every step and fall back to English") {
    const auto catalog = lessons::makeDefaultLessonCatalog();
    const auto localization = lessons::makeDefaultLessonLocalization();
    for (const auto lessonId : {
             "reflection_refraction",
             "thin_lens",
             "real_virtual_images",
             "diffraction"}) {
        for (const auto& step : catalog.lesson(lessonId).steps) {
            CHECK(localization.contains(
                lessons::LessonLocale::SimplifiedChinese, step.titleKey));
            CHECK(localization.contains(
                lessons::LessonLocale::SimplifiedChinese, step.instructionKey));
            CHECK(localization.contains(
                lessons::LessonLocale::SimplifiedChinese, step.contextKey));
        }
    }
    const lessons::LocalizationCatalog fallback({
        {lessons::LessonLocale::English, "test.key", "English"},
    });
    CHECK(fallback.text(
        lessons::LessonLocale::SimplifiedChinese, "test.key") == "English");
}

TEST_CASE("localization rejects unstable duplicate empty and missing messages") {
    CHECK_THROWS_AS(
        lessons::LocalizationCatalog({
            {lessons::LessonLocale::English, "bad key", "Text"},
        }),
        std::invalid_argument);
    CHECK_THROWS_AS(
        lessons::LocalizationCatalog({
            {lessons::LessonLocale::English, "test.key", ""},
        }),
        std::invalid_argument);
    CHECK_THROWS_AS(
        lessons::LocalizationCatalog({
            {lessons::LessonLocale::English, "test.key", "One"},
            {lessons::LessonLocale::English, "test.key", "Two"},
        }),
        std::invalid_argument);
    const auto localization = lessons::makeDefaultLessonLocalization();
    CHECK_THROWS_AS(
        static_cast<void>(localization.text(
            lessons::LessonLocale::English, "missing.key")),
        std::invalid_argument);
}

} // TEST_SUITE("LessonLocalization")
