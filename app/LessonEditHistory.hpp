#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "app/SamplingDebuggerPipeline.hpp"
#include "app/SlmInterferencePipeline.hpp"
#include "app/WaveDetectorPipeline.hpp"
#include "core/project/ProjectProvenance.hpp"
#include "optics/ray/BenchTracer.hpp"
#include "optics/scene/OpticalBenchScene.hpp"

namespace holobench::app {

// A snapshot contains only learner-editable physics inputs. Numerical results,
// display choices, and lesson progress deliberately have separate lifecycles.
struct LessonEditState final {
    optics::scene::OpticalBenchScene scene;
    project::ProjectProvenance sceneProvenance;
    optics::ray::BenchTracerOptions tracerOptions;
    wave::WaveDetectorConfig waveDetectorDraft;
    samplingdebug::SamplingDebuggerConfig samplingDebugger;
    project::ProjectProvenance waveProjectProvenance;
    std::string waveProjectName;
    slmexperiment::SlmInterferenceExperimentConfig slmInterferenceDraft;
    std::string slmCalibrationSource;
};

[[nodiscard]] bool sameLessonEditState(
    const LessonEditState& lhs,
    const LessonEditState& rhs) noexcept;

class LessonEditHistory final {
public:
    static constexpr std::size_t kDefaultCapacity = 64U;

    explicit LessonEditHistory(std::size_t capacity = kDefaultCapacity);

    void reset(LessonEditState initialState);
    [[nodiscard]] bool record(LessonEditState state);

    [[nodiscard]] bool canUndo() const noexcept;
    [[nodiscard]] bool canRedo() const noexcept;
    [[nodiscard]] const LessonEditState& undo();
    [[nodiscard]] const LessonEditState& redo();
    [[nodiscard]] const LessonEditState& current() const;

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
    std::vector<LessonEditState> states_;
    std::size_t cursor_ = 0U;
};

} // namespace holobench::app
