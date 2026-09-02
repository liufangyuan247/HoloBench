#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "app/ChimeraExposurePlan.hpp"

namespace holobench::compute::fft {
class IFftBackend;
}

namespace holobench::app::chimera {

inline constexpr int kChimeraBatchFormatVersion = 1;
inline constexpr std::size_t kMaximumChimeraBatchHogels = 1'000'000U;

enum class ChimeraBatchStopReason {
  Ready,
  SliceLimit,
  Cancelled,
  Complete,
};

struct ChimeraBatchChannelEvidence final {
  std::string channelId;
  double wavelengthMetres = 0.0;
  double diffractionEfficiency = 0.0;

  bool operator==(const ChimeraBatchChannelEvidence &) const = default;
};

struct ChimeraBatchHogelEvidence final {
  std::size_t hogelX = 0;
  std::size_t hogelY = 0;
  double stageXMetres = 0.0;
  double stageYMetres = 0.0;
  std::array<ChimeraBatchChannelEvidence, 3> channels;

  bool operator==(const ChimeraBatchHogelEvidence &) const = default;
};

struct ChimeraBatchArtifact final {
  int formatVersion = kChimeraBatchFormatVersion;
  std::string batchId = "chimera-print-batch";
  std::string sourceRecipeId;
  std::string sourceDatasetId;
  std::string sourceDatasetHash;
  std::string sourceExposurePlanId;
  std::string sourceExposurePlanHash;
  std::string sourceBenchProjectId;
  std::uint64_t sourceSceneRevision = 0;
  std::size_t hogelCountX = 0;
  std::size_t hogelCountY = 0;
  std::size_t nextLinearHogelIndex = 0;
  std::vector<ChimeraBatchHogelEvidence> completedHogels;
  ChimeraBatchStopReason stopReason = ChimeraBatchStopReason::Ready;
  std::string hashAlgorithm = std::string(kHogelDatasetHashAlgorithm);
  std::string contentHash;

  [[nodiscard]] std::size_t totalHogelCount() const noexcept;
  [[nodiscard]] double progressFraction() const noexcept;
  [[nodiscard]] bool complete() const noexcept;
  bool operator==(const ChimeraBatchArtifact &) const = default;
};

struct ChimeraBatchSliceResult final {
  std::vector<ExecutedHogelExposure> executedHogels;
  std::size_t completedHogelCount = 0;
  std::size_t totalHogelCount = 0;
  ChimeraBatchStopReason stopReason = ChimeraBatchStopReason::Ready;
};

[[nodiscard]] ChimeraBatchArtifact
createChimeraBatchArtifact(std::string batchId, const ChimeraRecipe &recipe,
                           const HogelDataset &dataset,
                           const ExposurePlan &plan, const BenchProject &bench);

void validateChimeraBatchArtifact(const ChimeraBatchArtifact &artifact);
void validateChimeraBatchForWorkflow(
    const ChimeraBatchArtifact &artifact, const ChimeraRecipe &recipe,
    const HogelDataset &dataset, const ExposurePlan &plan,
    const BenchProject &bench);
[[nodiscard]] std::string
computeChimeraBatchContentHash(const ChimeraBatchArtifact &artifact);
[[nodiscard]] std::string
serializeChimeraBatchArtifact(const ChimeraBatchArtifact &artifact);
[[nodiscard]] ChimeraBatchArtifact
parseChimeraBatchArtifact(std::string_view jsonText);
void saveChimeraBatchArtifact(const ChimeraBatchArtifact &artifact,
                              const std::filesystem::path &path);
[[nodiscard]] ChimeraBatchArtifact
loadChimeraBatchArtifact(const std::filesystem::path &path);

// Executes a bounded row-major slice. Cancellation is observed only between
// hogels so a checkpoint never contains a partial RGB exposure. Returned
// exposures retain detached diagnostics and compact reconstruction evidence,
// but discard the sampled object/reference wavefronts after summarization so
// slice memory does not scale by six complex fields per completed RGB hogel.
[[nodiscard]] ChimeraBatchSliceResult
runChimeraBatchSlice(ChimeraBatchArtifact &artifact,
                     const ChimeraRecipe &recipe, const HogelDataset &dataset,
                     const ExposurePlan &plan, const BenchProject &bench,
                     compute::fft::IFftBackend &fftBackend,
                     std::size_t maximumHogels,
                     const HogelExposureExecutionOptions &options = {},
                     const std::atomic_bool *cancellationRequested = nullptr);

// Restores the compact evidence required by the directional reconstruction
// contract without pretending to restore discarded sampled complex fields.
[[nodiscard]] std::vector<ExecutedHogelExposure>
restoreChimeraBatchReconstructionEvidence(const ChimeraBatchArtifact &artifact);

} // namespace holobench::app::chimera
