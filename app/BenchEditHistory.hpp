#pragma once

#include <cstddef>
#include <vector>

#include "app/BenchProject.hpp"

namespace holobench::app {

[[nodiscard]] bool sameBenchEditState(
    const BenchProject& lhs,
    const BenchProject& rhs) noexcept;

[[nodiscard]] BenchProject rebaseBenchEditStateRevision(
    const BenchProject& snapshot,
    optics::scene::SceneRevision currentRevision);

class BenchEditHistory final {
public:
    static constexpr std::size_t kDefaultCapacity = 64U;

    explicit BenchEditHistory(std::size_t capacity = kDefaultCapacity);

    void reset(BenchProject initialState);
    [[nodiscard]] bool record(BenchProject state);

    [[nodiscard]] bool canUndo() const noexcept;
    [[nodiscard]] bool canRedo() const noexcept;
    [[nodiscard]] const BenchProject& undo();
    [[nodiscard]] const BenchProject& redo();
    [[nodiscard]] const BenchProject& current() const;

    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }
    [[nodiscard]] std::size_t storedStateCount() const noexcept {
        return states_.size();
    }
    [[nodiscard]] std::size_t undoDepth() const noexcept { return cursor_; }
    [[nodiscard]] std::size_t redoDepth() const noexcept {
        return states_.empty() ? 0U : states_.size() - cursor_ - 1U;
    }

private:
    std::size_t capacity_;
    std::vector<BenchProject> states_;
    std::size_t cursor_ = 0U;
};

} // namespace holobench::app
