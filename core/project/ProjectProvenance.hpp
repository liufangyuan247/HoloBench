#pragma once

#include <string>
#include <string_view>

namespace holobench::project {

enum class ProjectOriginKind {
    User,
    LessonTemplate,
};

struct ProjectProvenance final {
    ProjectOriginKind originKind = ProjectOriginKind::User;
    std::string sourceId;
    int sourceVersion = 0;

    bool operator==(const ProjectProvenance&) const = default;
};

[[nodiscard]] bool isStableProjectSourceId(std::string_view value) noexcept;
void validateProjectProvenance(const ProjectProvenance& provenance);
[[nodiscard]] ProjectProvenance makeLessonTemplateProvenance(
    std::string sourceId,
    int sourceVersion = 1);
[[nodiscard]] std::string_view projectOriginKindName(
    ProjectOriginKind kind) noexcept;
[[nodiscard]] ProjectOriginKind projectOriginKindFromName(
    std::string_view name);

} // namespace holobench::project
