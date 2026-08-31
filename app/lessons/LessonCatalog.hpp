#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace holobench::app::lessons {

struct LessonStep final {
    std::string id;
    std::string titleKey;
    std::string instructionKey;
    std::string contextKey;
};

struct LessonDefinition final {
    std::string id;
    std::string titleKey;
    std::string objectiveKey;
    std::string projectTemplateId;
    std::vector<std::string> prerequisiteIds;
    std::vector<LessonStep> steps;
    bool advanced = false;
};

class LessonCatalog final {
public:
    explicit LessonCatalog(std::vector<LessonDefinition> lessons);

    [[nodiscard]] const std::vector<LessonDefinition>& lessons() const noexcept {
        return lessons_;
    }
    [[nodiscard]] bool contains(std::string_view lessonId) const noexcept;
    [[nodiscard]] const LessonDefinition& lesson(std::string_view lessonId) const;

private:
    std::vector<LessonDefinition> lessons_;
};

[[nodiscard]] bool isStableLessonKey(std::string_view value) noexcept;
[[nodiscard]] LessonCatalog makeDefaultLessonCatalog();

} // namespace holobench::app::lessons
