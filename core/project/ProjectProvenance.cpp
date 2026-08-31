#include "core/project/ProjectProvenance.hpp"

#include <stdexcept>
#include <utility>

namespace holobench::project {

bool isStableProjectSourceId(std::string_view value) noexcept {
    if (value.empty()) {
        return false;
    }
    const auto isAsciiLetterOrDigit = [](char character) {
        return (character >= 'a' && character <= 'z')
            || (character >= '0' && character <= '9');
    };
    if (!isAsciiLetterOrDigit(value.front())
        || !isAsciiLetterOrDigit(value.back())) {
        return false;
    }
    bool previousWasSeparator = false;
    for (const char character : value) {
        const bool separator = character == '_' || character == '-'
            || character == '.';
        if (!isAsciiLetterOrDigit(character) && !separator) {
            return false;
        }
        if (separator && previousWasSeparator) {
            return false;
        }
        previousWasSeparator = separator;
    }
    return true;
}

void validateProjectProvenance(const ProjectProvenance& provenance) {
    switch (provenance.originKind) {
    case ProjectOriginKind::User:
        if (!provenance.sourceId.empty() || provenance.sourceVersion != 0) {
            throw std::invalid_argument(
                "user project provenance cannot claim a source ID or version");
        }
        return;
    case ProjectOriginKind::LessonTemplate:
        if (!isStableProjectSourceId(provenance.sourceId)
            || provenance.sourceVersion < 1) {
            throw std::invalid_argument(
                "lesson-template provenance requires a stable source ID and positive version");
        }
        return;
    }
    throw std::invalid_argument("unsupported project provenance origin");
}

ProjectProvenance makeLessonTemplateProvenance(
    std::string sourceId,
    int sourceVersion) {
    ProjectProvenance result {
        .originKind = ProjectOriginKind::LessonTemplate,
        .sourceId = std::move(sourceId),
        .sourceVersion = sourceVersion,
    };
    validateProjectProvenance(result);
    return result;
}

std::string_view projectOriginKindName(ProjectOriginKind kind) noexcept {
    switch (kind) {
    case ProjectOriginKind::User:
        return "user";
    case ProjectOriginKind::LessonTemplate:
        return "lesson_template";
    }
    return "unknown";
}

ProjectOriginKind projectOriginKindFromName(std::string_view name) {
    if (name == "user") {
        return ProjectOriginKind::User;
    }
    if (name == "lesson_template") {
        return ProjectOriginKind::LessonTemplate;
    }
    throw std::invalid_argument("unsupported project provenance origin: "
        + std::string(name));
}

} // namespace holobench::project
