#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

#include "app/lessons/LessonProgress.hpp"

namespace lessons = holobench::app::lessons;

namespace {

void completeLesson(
    const lessons::LessonCatalog& catalog,
    lessons::LessonProgress& progress,
    std::string_view lessonId) {
    const auto& steps = catalog.lesson(lessonId).steps;
    while (lessons::nextLessonStepIndex(catalog, progress, lessonId) < steps.size()) {
        const auto nextIndex = lessons::nextLessonStepIndex(catalog, progress, lessonId);
        lessons::completeLessonStep(catalog, progress, lessonId, steps[nextIndex].id);
    }
}

class TemporaryProgressFile final {
public:
    TemporaryProgressFile()
        : path_(std::filesystem::temp_directory_path()
            / "holobench_lesson_progress_test.json") {
        std::error_code error;
        std::filesystem::remove(path_, error);
    }
    ~TemporaryProgressFile() {
        std::error_code error;
        std::filesystem::remove(path_, error);
    }
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

} // namespace

TEST_SUITE("LessonProgress") {

TEST_CASE("prerequisites lock lessons and completion unlocks dependent branches") {
    const auto catalog = lessons::makeDefaultLessonCatalog();
    lessons::LessonProgress progress;
    CHECK(lessons::lessonStatus(catalog, progress, "reflection_refraction")
        == lessons::LessonStatus::Available);
    CHECK(lessons::lessonStatus(catalog, progress, "thin_lens")
        == lessons::LessonStatus::Locked);
    CHECK(lessons::lessonStatus(catalog, progress, "diffraction")
        == lessons::LessonStatus::Locked);
    CHECK_THROWS_AS(
        lessons::completeLessonStep(catalog, progress, "thin_lens", "place_lens"),
        std::invalid_argument);

    lessons::completeLessonStep(
        catalog, progress, "reflection_refraction", "inspect_interface");
    CHECK(lessons::lessonStatus(catalog, progress, "reflection_refraction")
        == lessons::LessonStatus::InProgress);
    completeLesson(catalog, progress, "reflection_refraction");
    CHECK(lessons::lessonStatus(catalog, progress, "reflection_refraction")
        == lessons::LessonStatus::Completed);
    CHECK(lessons::lessonStatus(catalog, progress, "thin_lens")
        == lessons::LessonStatus::Available);
    CHECK(lessons::lessonStatus(catalog, progress, "diffraction")
        == lessons::LessonStatus::Available);
}

TEST_CASE("steps are an ordered prefix and completed lessons reject extra work") {
    const auto catalog = lessons::makeDefaultLessonCatalog();
    lessons::LessonProgress progress;
    CHECK(lessons::nextLessonStepIndex(
        catalog, progress, "reflection_refraction") == 0U);
    CHECK_THROWS_AS(
        lessons::completeLessonStep(
            catalog, progress, "reflection_refraction", "observe_snell"),
        std::invalid_argument);
    completeLesson(catalog, progress, "reflection_refraction");
    CHECK(lessons::nextLessonStepIndex(
        catalog, progress, "reflection_refraction") == 3U);
    CHECK_THROWS_AS(
        lessons::completeLessonStep(
            catalog, progress, "reflection_refraction", "observe_snell"),
        std::invalid_argument);
}

TEST_CASE("reset cascades through transitive dependents but preserves other branches") {
    const auto catalog = lessons::makeDefaultLessonCatalog();
    lessons::LessonProgress progress;
    completeLesson(catalog, progress, "reflection_refraction");
    completeLesson(catalog, progress, "thin_lens");
    completeLesson(catalog, progress, "diffraction");
    completeLesson(catalog, progress, "fourier_plane");
    lessons::completeLessonStep(
        catalog, progress, "spatial_filtering", "inspect_spectrum");

    lessons::resetLessonAndDependents(catalog, progress, "thin_lens");
    CHECK(lessons::lessonStatus(catalog, progress, "reflection_refraction")
        == lessons::LessonStatus::Completed);
    CHECK(lessons::lessonStatus(catalog, progress, "diffraction")
        == lessons::LessonStatus::Completed);
    CHECK(lessons::lessonStatus(catalog, progress, "thin_lens")
        == lessons::LessonStatus::Available);
    CHECK(lessons::lessonStatus(catalog, progress, "fourier_plane")
        == lessons::LessonStatus::Locked);
    CHECK_FALSE(progress.completedStepIds.contains("spatial_filtering"));

    lessons::resetAllLessonProgress(progress);
    CHECK(progress.completedStepIds.empty());
}

TEST_CASE("progress JSON and file persistence are semantic and byte stable") {
    const auto catalog = lessons::makeDefaultLessonCatalog();
    lessons::LessonProgress progress;
    completeLesson(catalog, progress, "reflection_refraction");
    lessons::completeLessonStep(catalog, progress, "thin_lens", "place_lens");

    const auto first = lessons::serializeLessonProgressJson(catalog, progress);
    const auto restored = lessons::deserializeLessonProgressJson(catalog, first);
    const auto second = lessons::serializeLessonProgressJson(catalog, restored);
    CHECK(first == second);
    CHECK(restored.completedStepIds == progress.completedStepIds);

    TemporaryProgressFile file;
    lessons::saveLessonProgress(file.path(), catalog, progress);
    const auto loaded = lessons::loadLessonProgress(file.path(), catalog);
    CHECK(loaded.completedStepIds == progress.completedStepIds);
    std::ifstream stream(file.path(), std::ios::binary);
    const std::string fileText {
        std::istreambuf_iterator<char>(stream),
        std::istreambuf_iterator<char>()};
    CHECK(fileText == first);
}

TEST_CASE("strict progress parser rejects schema drift corruption and impossible state") {
    const auto catalog = lessons::makeDefaultLessonCatalog();
    const auto validText = lessons::serializeLessonProgressJson(catalog, {});
    auto json = nlohmann::json::parse(validText);
    json["unknown"] = true;
    CHECK_THROWS_AS(
        static_cast<void>(lessons::deserializeLessonProgressJson(catalog, json.dump())),
        std::invalid_argument);

    json = nlohmann::json::parse(validText);
    json["format_version"] = 2;
    CHECK_THROWS_AS(
        static_cast<void>(lessons::deserializeLessonProgressJson(catalog, json.dump())),
        std::invalid_argument);

    json = nlohmann::json::parse(validText);
    json["kind"] = "physics_project";
    CHECK_THROWS_AS(
        static_cast<void>(lessons::deserializeLessonProgressJson(catalog, json.dump())),
        std::invalid_argument);

    json = nlohmann::json::parse(validText);
    json["lessons"].push_back({
        {"completed_steps", {"inspect_interface"}},
        {"lesson_id", "missing"},
    });
    CHECK_THROWS_AS(
        static_cast<void>(lessons::deserializeLessonProgressJson(catalog, json.dump())),
        std::invalid_argument);

    json = nlohmann::json::parse(validText);
    json["lessons"].push_back({
        {"completed_steps", {"move_screen"}},
        {"lesson_id", "thin_lens"},
    });
    CHECK_THROWS_AS(
        static_cast<void>(lessons::deserializeLessonProgressJson(catalog, json.dump())),
        std::invalid_argument);

    CHECK_THROWS_AS(
        static_cast<void>(lessons::deserializeLessonProgressJson(catalog, "{")),
        std::invalid_argument);
}

} // TEST_SUITE("LessonProgress")
