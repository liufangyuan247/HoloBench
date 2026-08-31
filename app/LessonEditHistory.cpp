#include "app/LessonEditHistory.hpp"

#include <stdexcept>
#include <utility>

#include "app/SlmInterferenceUiState.hpp"

namespace holobench::app {
namespace {

[[nodiscard]] bool sameTracerOptions(
    const optics::ray::BenchTracerOptions& lhs,
    const optics::ray::BenchTracerOptions& rhs) noexcept {
    return lhs.rayCount == rhs.rayCount
        && lhs.pattern == rhs.pattern
        && lhs.maxPropagationDistanceMetres
            == rhs.maxPropagationDistanceMetres
        && lhs.includeVirtualExtensions == rhs.includeVirtualExtensions
        && lhs.virtualExtensionDistanceMetres
            == rhs.virtualExtensionDistanceMetres;
}

} // namespace

bool sameLessonEditState(
    const LessonEditState& lhs,
    const LessonEditState& rhs) noexcept {
    return lhs.scene == rhs.scene
        && lhs.sceneProvenance == rhs.sceneProvenance
        && sameTracerOptions(lhs.tracerOptions, rhs.tracerOptions)
        && lhs.waveDetectorDraft == rhs.waveDetectorDraft
        && lhs.samplingDebugger == rhs.samplingDebugger
        && slmui::sameExperimentPhysicsConfig(
            lhs.slmInterferenceDraft, rhs.slmInterferenceDraft)
        && lhs.slmCalibrationSource == rhs.slmCalibrationSource;
}

LessonEditHistory::LessonEditHistory(std::size_t capacity)
    : capacity_(capacity) {
    if (capacity_ == 0U) {
        throw std::invalid_argument(
            "lesson edit history capacity must be at least one");
    }
    states_.reserve(capacity_);
}

void LessonEditHistory::reset(LessonEditState initialState) {
    states_.clear();
    states_.push_back(std::move(initialState));
    cursor_ = 0U;
}

bool LessonEditHistory::record(LessonEditState state) {
    if (states_.empty()) {
        reset(std::move(state));
        return true;
    }
    if (sameLessonEditState(states_[cursor_], state)) {
        return false;
    }

    states_.erase(states_.begin() + static_cast<std::ptrdiff_t>(cursor_ + 1U),
        states_.end());
    if (states_.size() == capacity_) {
        states_.erase(states_.begin());
        if (cursor_ > 0U) {
            --cursor_;
        }
    }
    states_.push_back(std::move(state));
    cursor_ = states_.size() - 1U;
    return true;
}

bool LessonEditHistory::canUndo() const noexcept {
    return !states_.empty() && cursor_ > 0U;
}

bool LessonEditHistory::canRedo() const noexcept {
    return !states_.empty() && cursor_ + 1U < states_.size();
}

const LessonEditState& LessonEditHistory::undo() {
    if (!canUndo()) {
        throw std::logic_error("lesson edit history has no state to undo");
    }
    return states_[--cursor_];
}

const LessonEditState& LessonEditHistory::redo() {
    if (!canRedo()) {
        throw std::logic_error("lesson edit history has no state to redo");
    }
    return states_[++cursor_];
}

const LessonEditState& LessonEditHistory::current() const {
    if (states_.empty()) {
        throw std::logic_error("lesson edit history has not been initialized");
    }
    return states_[cursor_];
}

} // namespace holobench::app
