#include "app/lessons/LessonProgress.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <set>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace holobench::app::lessons {
namespace {

using Json = nlohmann::json;

void requireKeys(
    const Json& object,
    const std::set<std::string>& expected,
    const char* context) {
    if (!object.is_object()) {
        throw std::invalid_argument(std::string(context) + " must be an object");
    }
    std::set<std::string> actual;
    for (auto iterator = object.begin(); iterator != object.end(); ++iterator) {
        actual.insert(iterator.key());
    }
    if (actual != expected) {
        throw std::invalid_argument(std::string(context) + " has missing or unknown keys");
    }
}

[[nodiscard]] bool isCompleteUnchecked(
    const LessonCatalog& catalog,
    const LessonProgress& progress,
    std::string_view lessonId) {
    const auto found = progress.completedStepIds.find(std::string(lessonId));
    return found != progress.completedStepIds.end()
        && found->second.size() == catalog.lesson(lessonId).steps.size();
}

[[nodiscard]] bool isUnlockedUnchecked(
    const LessonCatalog& catalog,
    const LessonProgress& progress,
    const LessonDefinition& definition) {
    return std::ranges::all_of(definition.prerequisiteIds, [&](const auto& prerequisiteId) {
        return isCompleteUnchecked(catalog, progress, prerequisiteId);
    });
}

} // namespace

void validateLessonProgress(
    const LessonCatalog& catalog,
    const LessonProgress& progress) {
    if (progress.formatVersion != kLessonProgressFormatVersion) {
        throw std::invalid_argument("unsupported lesson progress format version");
    }
    for (const auto& [lessonId, completedSteps] : progress.completedStepIds) {
        const auto& definition = catalog.lesson(lessonId);
        if (completedSteps.empty()) {
            throw std::invalid_argument("lesson progress must omit empty entries");
        }
        if (completedSteps.size() > definition.steps.size()) {
            throw std::invalid_argument("lesson progress contains too many completed steps");
        }
        for (std::size_t index = 0; index < completedSteps.size(); ++index) {
            if (completedSteps[index] != definition.steps[index].id) {
                throw std::invalid_argument("lesson progress must contain an ordered step prefix");
            }
        }
        if (!isUnlockedUnchecked(catalog, progress, definition)) {
            throw std::invalid_argument("lesson progress exists while prerequisites are incomplete");
        }
    }
}

LessonStatus lessonStatus(
    const LessonCatalog& catalog,
    const LessonProgress& progress,
    std::string_view lessonId) {
    validateLessonProgress(catalog, progress);
    const auto& definition = catalog.lesson(lessonId);
    if (!isUnlockedUnchecked(catalog, progress, definition)) {
        return LessonStatus::Locked;
    }
    const auto found = progress.completedStepIds.find(std::string(lessonId));
    if (found == progress.completedStepIds.end()) {
        return LessonStatus::Available;
    }
    if (found->second.size() == definition.steps.size()) {
        return LessonStatus::Completed;
    }
    return LessonStatus::InProgress;
}

std::size_t nextLessonStepIndex(
    const LessonCatalog& catalog,
    const LessonProgress& progress,
    std::string_view lessonId) {
    validateLessonProgress(catalog, progress);
    static_cast<void>(catalog.lesson(lessonId));
    const auto found = progress.completedStepIds.find(std::string(lessonId));
    return found == progress.completedStepIds.end() ? 0U : found->second.size();
}

void completeLessonStep(
    const LessonCatalog& catalog,
    LessonProgress& progress,
    std::string_view lessonId,
    std::string_view stepId) {
    validateLessonProgress(catalog, progress);
    const auto& definition = catalog.lesson(lessonId);
    if (!isUnlockedUnchecked(catalog, progress, definition)) {
        throw std::invalid_argument("cannot complete a step in a locked lesson");
    }
    const auto found = progress.completedStepIds.find(definition.id);
    const std::size_t nextIndex = found == progress.completedStepIds.end()
        ? 0U
        : found->second.size();
    if (nextIndex >= definition.steps.size()) {
        throw std::invalid_argument("lesson is already complete");
    }
    if (definition.steps[nextIndex].id != stepId) {
        throw std::invalid_argument("lesson steps must be completed in catalog order");
    }
    progress.completedStepIds[definition.id].push_back(std::string(stepId));
}

void resetLessonAndDependents(
    const LessonCatalog& catalog,
    LessonProgress& progress,
    std::string_view lessonId) {
    validateLessonProgress(catalog, progress);
    static_cast<void>(catalog.lesson(lessonId));
    std::set<std::string> resetIds {std::string(lessonId)};
    bool changed = true;
    while (changed) {
        changed = false;
        for (const auto& definition : catalog.lessons()) {
            const bool dependsOnReset = std::ranges::any_of(
                definition.prerequisiteIds,
                [&](const auto& prerequisiteId) { return resetIds.contains(prerequisiteId); });
            if (dependsOnReset && resetIds.insert(definition.id).second) {
                changed = true;
            }
        }
    }
    for (const auto& resetId : resetIds) {
        progress.completedStepIds.erase(resetId);
    }
}

void resetAllLessonProgress(LessonProgress& progress) noexcept {
    progress.formatVersion = kLessonProgressFormatVersion;
    progress.completedStepIds.clear();
}

std::string serializeLessonProgressJson(
    const LessonCatalog& catalog,
    const LessonProgress& progress) {
    validateLessonProgress(catalog, progress);
    Json lessons = Json::array();
    for (const auto& definition : catalog.lessons()) {
        const auto found = progress.completedStepIds.find(definition.id);
        if (found != progress.completedStepIds.end()) {
            lessons.push_back({
                {"completed_steps", found->second},
                {"lesson_id", definition.id},
            });
        }
    }
    const Json root {
        {"format_version", progress.formatVersion},
        {"kind", "holobench_lesson_progress"},
        {"lessons", std::move(lessons)},
    };
    return root.dump(2) + "\n";
}

LessonProgress deserializeLessonProgressJson(
    const LessonCatalog& catalog,
    std::string_view jsonText) {
    try {
        const Json root = Json::parse(jsonText);
        requireKeys(root, {"format_version", "kind", "lessons"}, "lesson progress");
        if (!root.at("format_version").is_number_integer()
            || root.at("format_version").get<int>() != kLessonProgressFormatVersion) {
            throw std::invalid_argument("unsupported lesson progress format version");
        }
        if (!root.at("kind").is_string()
            || root.at("kind").get<std::string>() != "holobench_lesson_progress") {
            throw std::invalid_argument("unsupported lesson progress kind");
        }
        if (!root.at("lessons").is_array()) {
            throw std::invalid_argument("lesson progress lessons must be an array");
        }

        LessonProgress progress;
        for (const auto& value : root.at("lessons")) {
            requireKeys(value, {"completed_steps", "lesson_id"}, "lesson progress entry");
            if (!value.at("lesson_id").is_string()
                || !value.at("completed_steps").is_array()) {
                throw std::invalid_argument("lesson progress entry has invalid value types");
            }
            const auto lessonId = value.at("lesson_id").get<std::string>();
            std::vector<std::string> completedSteps;
            for (const auto& stepValue : value.at("completed_steps")) {
                if (!stepValue.is_string()) {
                    throw std::invalid_argument("completed lesson step ID must be a string");
                }
                completedSteps.push_back(stepValue.get<std::string>());
            }
            if (!progress.completedStepIds.emplace(lessonId, std::move(completedSteps)).second) {
                throw std::invalid_argument("lesson progress contains a duplicate lesson ID");
            }
        }
        validateLessonProgress(catalog, progress);
        return progress;
    } catch (const std::invalid_argument&) {
        throw;
    } catch (const Json::exception& exception) {
        throw std::invalid_argument(
            std::string("invalid lesson progress JSON: ") + exception.what());
    }
}

void saveLessonProgress(
    const std::filesystem::path& path,
    const LessonCatalog& catalog,
    const LessonProgress& progress) {
    const auto text = serializeLessonProgressJson(catalog, progress);
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) {
        throw std::runtime_error("cannot open lesson progress for writing");
    }
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!stream) {
        throw std::runtime_error("failed to write lesson progress");
    }
}

LessonProgress loadLessonProgress(
    const std::filesystem::path& path,
    const LessonCatalog& catalog) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("cannot open lesson progress for reading");
    }
    const std::string text {
        std::istreambuf_iterator<char>(stream),
        std::istreambuf_iterator<char>()};
    if (!stream.eof() && stream.fail()) {
        throw std::runtime_error("failed to read lesson progress");
    }
    return deserializeLessonProgressJson(catalog, text);
}

} // namespace holobench::app::lessons
