#include "app/BenchEditHistory.hpp"

#include <limits>
#include <stdexcept>
#include <utility>

namespace holobench::app {

bool sameBenchEditState(
    const BenchProject& lhs,
    const BenchProject& rhs) noexcept {
    return lhs.formatVersion == rhs.formatVersion
        && lhs.projectId == rhs.projectId
        && lhs.name == rhs.name
        && lhs.provenance == rhs.provenance
        && lhs.scene.components() == rhs.scene.components();
}

BenchProject rebaseBenchEditStateRevision(
    const BenchProject& snapshot,
    optics::scene::SceneRevision currentRevision) {
    if (currentRevision == std::numeric_limits<optics::scene::SceneRevision>::max()) {
        throw std::overflow_error(
            "bench scene revision exhausted while restoring history");
    }
    BenchProject result = snapshot;
    result.scene = optics::scene::BenchScene(
        result.scene.components(), currentRevision + 1U);
    validateBenchProject(result);
    return result;
}

BenchEditHistory::BenchEditHistory(std::size_t capacity)
    : capacity_(capacity) {
    if (capacity_ == 0U) {
        throw std::invalid_argument(
            "bench edit history capacity must be at least one");
    }
    states_.reserve(capacity_);
}

void BenchEditHistory::reset(BenchProject initialState) {
    validateBenchProject(initialState);
    states_.clear();
    states_.push_back(std::move(initialState));
    cursor_ = 0U;
}

bool BenchEditHistory::record(BenchProject state) {
    validateBenchProject(state);
    if (states_.empty()) {
        reset(std::move(state));
        return true;
    }
    if (sameBenchEditState(states_[cursor_], state)) {
        return false;
    }

    states_.erase(
        states_.begin() + static_cast<std::ptrdiff_t>(cursor_ + 1U),
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

bool BenchEditHistory::canUndo() const noexcept {
    return !states_.empty() && cursor_ > 0U;
}

bool BenchEditHistory::canRedo() const noexcept {
    return !states_.empty() && cursor_ + 1U < states_.size();
}

const BenchProject& BenchEditHistory::undo() {
    if (!canUndo()) {
        throw std::logic_error("bench edit history has no state to undo");
    }
    return states_[--cursor_];
}

const BenchProject& BenchEditHistory::redo() {
    if (!canRedo()) {
        throw std::logic_error("bench edit history has no state to redo");
    }
    return states_[++cursor_];
}

const BenchProject& BenchEditHistory::current() const {
    if (states_.empty()) {
        throw std::logic_error("bench edit history has not been initialized");
    }
    return states_[cursor_];
}

} // namespace holobench::app
