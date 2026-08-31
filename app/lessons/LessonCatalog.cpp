#include "app/lessons/LessonCatalog.hpp"

#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>

namespace holobench::app::lessons {
namespace {

[[nodiscard]] LessonStep step(
    std::string id,
    std::string lessonKey,
    std::string stepKey) {
    const std::string prefix = "lesson." + lessonKey + ".step." + stepKey;
    return {
        .id = std::move(id),
        .titleKey = prefix + ".title",
        .instructionKey = prefix + ".instruction",
        .contextKey = prefix + ".context",
    };
}

[[nodiscard]] LessonDefinition lesson(
    std::string id,
    std::vector<std::string> prerequisites,
    std::vector<LessonStep> steps,
    bool advanced = false) {
    const std::string key = id;
    return {
        .id = std::move(id),
        .titleKey = "lesson." + key + ".title",
        .objectiveKey = "lesson." + key + ".objective",
        .projectTemplateId = "lesson_" + key,
        .prerequisiteIds = std::move(prerequisites),
        .steps = std::move(steps),
        .advanced = advanced,
    };
}

void validateLesson(const LessonDefinition& definition) {
    if (!isStableLessonKey(definition.id)
        || !isStableLessonKey(definition.titleKey)
        || !isStableLessonKey(definition.objectiveKey)
        || !isStableLessonKey(definition.projectTemplateId)) {
        throw std::invalid_argument("lesson identity and message keys must be stable ASCII keys");
    }
    if (definition.steps.empty()) {
        throw std::invalid_argument("lesson must define at least one step");
    }
    std::set<std::string> prerequisiteIds;
    for (const auto& prerequisiteId : definition.prerequisiteIds) {
        if (!isStableLessonKey(prerequisiteId)
            || !prerequisiteIds.insert(prerequisiteId).second) {
            throw std::invalid_argument("lesson prerequisite IDs must be stable and unique");
        }
    }
    std::set<std::string> stepIds;
    for (const auto& lessonStep : definition.steps) {
        if (!isStableLessonKey(lessonStep.id)
            || !isStableLessonKey(lessonStep.titleKey)
            || !isStableLessonKey(lessonStep.instructionKey)
            || !isStableLessonKey(lessonStep.contextKey)
            || !stepIds.insert(lessonStep.id).second) {
            throw std::invalid_argument("lesson step IDs and message keys must be stable and unique");
        }
    }
}

} // namespace

bool isStableLessonKey(std::string_view value) noexcept {
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
        const bool separator = character == '_' || character == '-' || character == '.';
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

LessonCatalog::LessonCatalog(std::vector<LessonDefinition> lessons)
    : lessons_(std::move(lessons)) {
    if (lessons_.empty()) {
        throw std::invalid_argument("lesson catalog must not be empty");
    }

    std::map<std::string, std::size_t> lessonIndices;
    for (std::size_t index = 0; index < lessons_.size(); ++index) {
        validateLesson(lessons_[index]);
        if (!lessonIndices.emplace(lessons_[index].id, index).second) {
            throw std::invalid_argument("lesson catalog contains a duplicate lesson ID");
        }
    }
    for (const auto& definition : lessons_) {
        for (const auto& prerequisiteId : definition.prerequisiteIds) {
            if (!lessonIndices.contains(prerequisiteId)) {
                throw std::invalid_argument("lesson catalog references an unknown prerequisite");
            }
        }
    }

    enum class VisitState { Unvisited, Visiting, Visited };
    std::vector<VisitState> states(lessons_.size(), VisitState::Unvisited);
    std::function<void(std::size_t)> visit = [&](std::size_t index) {
        if (states[index] == VisitState::Visiting) {
            throw std::invalid_argument("lesson prerequisite graph contains a cycle");
        }
        if (states[index] == VisitState::Visited) {
            return;
        }
        states[index] = VisitState::Visiting;
        for (const auto& prerequisiteId : lessons_[index].prerequisiteIds) {
            visit(lessonIndices.at(prerequisiteId));
        }
        states[index] = VisitState::Visited;
    };
    for (std::size_t index = 0; index < lessons_.size(); ++index) {
        visit(index);
    }
}

bool LessonCatalog::contains(std::string_view lessonId) const noexcept {
    return std::ranges::any_of(lessons_, [&](const auto& definition) {
        return definition.id == lessonId;
    });
}

const LessonDefinition& LessonCatalog::lesson(std::string_view lessonId) const {
    const auto found = std::ranges::find_if(lessons_, [&](const auto& definition) {
        return definition.id == lessonId;
    });
    if (found == lessons_.end()) {
        throw std::invalid_argument("unknown lesson ID");
    }
    return *found;
}

LessonCatalog makeDefaultLessonCatalog() {
    return LessonCatalog({
        lesson("reflection_refraction", {}, {
            step("inspect_interface", "reflection_refraction", "inspect_interface"),
            step("change_incidence", "reflection_refraction", "change_incidence"),
            step("observe_snell", "reflection_refraction", "observe_snell"),
        }),
        lesson("thin_lens", {"reflection_refraction"}, {
            step("place_lens", "thin_lens", "place_lens"),
            step("move_screen", "thin_lens", "move_screen"),
            step("compare_focus", "thin_lens", "compare_focus"),
        }),
        lesson("real_virtual_images", {"thin_lens"}, {
            step("form_real_image", "real_virtual_images", "form_real_image"),
            step("cross_focal_plane", "real_virtual_images", "cross_focal_plane"),
            step("classify_image", "real_virtual_images", "classify_image"),
        }),
        lesson("diffraction", {"reflection_refraction"}, {
            step("select_aperture", "diffraction", "select_aperture"),
            step("change_width", "diffraction", "change_width"),
            step("compare_pattern", "diffraction", "compare_pattern"),
        }),
        lesson("fourier_plane", {"thin_lens", "diffraction"}, {
            step("load_4f_template", "fourier_plane", "load_4f_template"),
            step("place_probe", "fourier_plane", "place_probe"),
            step("identify_spectrum", "fourier_plane", "identify_spectrum"),
        }),
        lesson("spatial_filtering", {"fourier_plane"}, {
            step("inspect_spectrum", "spatial_filtering", "inspect_spectrum"),
            step("apply_filter", "spatial_filtering", "apply_filter"),
            step("explain_image", "spatial_filtering", "explain_image"),
        }),
        lesson("na_psf", {"thin_lens", "diffraction"}, {
            step("inspect_aperture", "na_psf", "inspect_aperture"),
            step("change_na", "na_psf", "change_na"),
            step("compare_psf", "na_psf", "compare_psf"),
        }),
        lesson("coherence_interference", {"diffraction"}, {
            step("overlap_beams", "coherence_interference", "overlap_beams"),
            step("change_path_difference", "coherence_interference", "change_path_difference"),
            step("compare_visibility", "coherence_interference", "compare_visibility"),
        }),
        lesson("holography", {"coherence_interference"}, {
            step("record_hologram", "holography", "record_hologram"),
            step("replay_hologram", "holography", "replay_hologram"),
            step("identify_orders", "holography", "identify_orders"),
        }, true),
        lesson("h1_h2_advanced", {"holography"}, {
            step("record_h1", "h1_h2_advanced", "record_h1"),
            step("position_h2", "h1_h2_advanced", "position_h2"),
            step("observe_transplane", "h1_h2_advanced", "observe_transplane"),
        }, true),
    });
}

} // namespace holobench::app::lessons
