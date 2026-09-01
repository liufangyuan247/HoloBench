#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "app/ChimeraHogelDataset.hpp"
#include "optics/holography/BenchVolumeHologram.hpp"
#include "optics/holography/PlateFieldSampling.hpp"

namespace holobench::app::chimera {

inline constexpr int kExposurePlanFormatVersion = 1;

enum class ExposureEventKind {
    StageMove,
    SlmLoad,
    BeamGate,
    Exposure,
};

struct ExposureEvent final {
    std::string eventId;
    ExposureEventKind kind = ExposureEventKind::StageMove;
    double startSeconds = 0.0;
    double durationSeconds = 0.0;
    std::size_t hogelX = 0;
    std::size_t hogelY = 0;
    double stageXMetres = 0.0;
    double stageYMetres = 0.0;
    std::string channelId;
    double wavelengthMetres = 0.0;
    std::string slmComponentId;
    std::string slmCommandId;
    std::string objectSourceComponentId;
    std::string referenceSourceComponentId;
    bool objectBeamEnabled = false;
    bool referenceBeamEnabled = false;
    std::string recordingRecipeId;

    bool operator==(const ExposureEvent&) const = default;
};

struct ExposurePlan final {
    int formatVersion = kExposurePlanFormatVersion;
    std::string planId;
    std::string sourceRecipeId;
    int sourceRecipeVersion = kChimeraRecipeFormatVersion;
    std::string sourceDatasetId;
    std::string sourceDatasetHash;
    std::string sourceBenchProjectId;
    std::string timeUnit = "s";
    std::string lengthUnit = "m";
    std::vector<ExposureEvent> events;
    double totalDurationSeconds = 0.0;
    std::string hashAlgorithm = std::string(kHogelDatasetHashAlgorithm);
    std::string contentHash;

    bool operator==(const ExposurePlan&) const = default;
};

struct ExecutedHogelChannelExposure final {
    std::size_t hogelX = 0;
    std::size_t hogelY = 0;
    double stageXMetres = 0.0;
    double stageYMetres = 0.0;
    std::string channelId;
    std::string exposureEventId;
    std::string slmCommandId;
    std::string recordingRecipeId;
    bool m8VolumeRecordingInvoked = false;
    bool sparseSlmRasterTransferredToPlacedWavePath = false;
    std::size_t sampleWidth = 0;
    std::size_t sampleHeight = 0;
    bool usedBoundedPreviewSampling = false;
    optics::holography::PlateFieldSamplingDiagnostics objectFieldDiagnostics;
    optics::holography::VolumePlateRecordingResult recording;

};

struct HogelExposureExecutionOptions final {
    std::size_t maximumPreviewSampleWidth = 256;
    std::size_t maximumPreviewSampleHeight = 256;
};

struct ExecutedHogelExposure final {
    std::string planId;
    std::string planHash;
    std::size_t hogelX = 0;
    std::size_t hogelY = 0;
    std::vector<ExecutedHogelChannelExposure> channels;
};

[[nodiscard]] ExposurePlan generateExposurePlan(
    const ChimeraRecipe& recipe,
    const HogelDataset& dataset,
    const BenchProject& bench);

void validateExposurePlan(const ExposurePlan& plan);
[[nodiscard]] std::string computeExposurePlanContentHash(
    const ExposurePlan& plan);
[[nodiscard]] std::string serializeExposurePlan(const ExposurePlan& plan);
[[nodiscard]] ExposurePlan parseExposurePlan(std::string_view jsonText);

// Executes the selected event group through the public M8 sampled placed-wave
// path and volume-recording contracts. Sparse pixels are transient command
// artifacts bound to the persisted SLM component and command provenance.
[[nodiscard]] ExecutedHogelExposure executeHogelExposure(
    const ChimeraRecipe& recipe,
    const HogelDataset& dataset,
    const ExposurePlan& plan,
    const BenchProject& bench,
    compute::fft::IFftBackend& fftBackend,
    std::size_t hogelX,
    std::size_t hogelY,
    const HogelExposureExecutionOptions& options = {});

} // namespace holobench::app::chimera
