#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "app/lessons/LessonCatalog.hpp"

namespace holobench::app::lessons {

inline constexpr int kLessonProgressFormatVersion = 1;

enum class LessonStatus {
    Locked,
    Available,
    InProgress,
    Completed,
};

struct LessonProgress final {
    int formatVersion = kLessonProgressFormatVersion;
    std::map<std::string, std::vector<std::string>> completedStepIds;
};

void validateLessonProgress(
    const LessonCatalog& catalog,
    const LessonProgress& progress);
[[nodiscard]] LessonStatus lessonStatus(
    const LessonCatalog& catalog,
    const LessonProgress& progress,
    std::string_view lessonId);
[[nodiscard]] std::size_t nextLessonStepIndex(
    const LessonCatalog& catalog,
    const LessonProgress& progress,
    std::string_view lessonId);
void completeLessonStep(
    const LessonCatalog& catalog,
    LessonProgress& progress,
    std::string_view lessonId,
    std::string_view stepId);
void resetLessonAndDependents(
    const LessonCatalog& catalog,
    LessonProgress& progress,
    std::string_view lessonId);
void resetAllLessonProgress(LessonProgress& progress) noexcept;

[[nodiscard]] std::string serializeLessonProgressJson(
    const LessonCatalog& catalog,
    const LessonProgress& progress);
[[nodiscard]] LessonProgress deserializeLessonProgressJson(
    const LessonCatalog& catalog,
    std::string_view jsonText);
void saveLessonProgress(
    const std::filesystem::path& path,
    const LessonCatalog& catalog,
    const LessonProgress& progress);
[[nodiscard]] LessonProgress loadLessonProgress(
    const std::filesystem::path& path,
    const LessonCatalog& catalog);

} // namespace holobench::app::lessons
