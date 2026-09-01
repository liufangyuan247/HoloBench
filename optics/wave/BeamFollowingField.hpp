#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <vector>

#include "core/field/ComplexField2D.hpp"
#include "optics/scene/BenchPathEvidence.hpp"

namespace holobench::compute::fft {
class IFftBackend;
}

namespace holobench::optics::ray {
class ILensPrescriptionResolver;
}

namespace holobench::optics::slm {
class CalibratedSlmResponse;
}

namespace holobench::optics::wave {

struct PlacedSlmSparsePixel final {
    std::size_t column = 0;
    std::size_t row = 0;
    double normalizedCommand = 0.0;

    bool operator==(const PlacedSlmSparsePixel&) const = default;
};

struct PlacedSlmSparseCommand final {
    std::string componentId;
    std::string commandId;
    std::size_t pixelWidth = 0;
    std::size_t pixelHeight = 0;
    double defaultNormalizedCommand = 0.0;
    std::string calibrationId;
    const optics::slm::CalibratedSlmResponse* calibratedResponse = nullptr;
    std::vector<PlacedSlmSparsePixel> pixels;

    bool operator==(const PlacedSlmSparseCommand&) const = default;
};

struct BeamFollowingFieldOptions final {
    std::size_t sampleWidth = 128;
    std::size_t sampleHeight = 128;
    double extentWidthMetres = 0.01;
    double extentHeightMetres = 0.01;
    double centreXMetres = 0.0;
    double centreYMetres = 0.0;
    double refractiveIndex = 1.0;

    bool operator==(const BeamFollowingFieldOptions&) const = default;
};

struct BeamFollowingFieldDiagnostics final {
    std::size_t workingSampleWidth = 0;
    std::size_t workingSampleHeight = 0;
    std::size_t propagatedSegmentCount = 0;
    bool usedTiltedElementProjection = false;
    bool usedFoldedPath = false;
    bool usedTargetTangentProjection = false;
    bool supportTouchesBoundary = false;
    std::vector<std::string> appliedWaveComponentIds;
    std::vector<std::string> foldedWaveComponentIds;
    std::vector<std::string> appliedSlmCommandIds;
    std::vector<std::string> appliedSlmCalibrationIds;
    std::vector<std::string> appliedRealLensPrescriptionIds;
    std::vector<std::string> warnings;
};

struct BeamFollowingFieldResult final {
    field::ComplexField2D fieldAtTarget;
    BeamFollowingFieldDiagnostics diagnostics;
};

[[nodiscard]] bool requiresBeamFollowingWaveTransform(
    scene::BenchComponentKind kind) noexcept;

// Validates model coverage and ordered geometry without performing an FFT.
// Callers that expose an approximation fallback can retain the exact reason;
// measurement callers should surface the exception and reject partial light.
void validateBeamFollowingFieldPath(
    const scene::BenchScene& bench,
    const scene::BeamState& terminalBeam,
    std::span<const scene::BenchPathInteraction> pathInteractions,
    const ray::ILensPrescriptionResolver* lensPrescriptions = nullptr);

// Propagates one exact traced branch through its ordered physical path. The
// target is the final Screen, Field Probe, or Holographic Plate interaction.
// The field uses a 2x padded beam-normal working grid, applies supported thin
// elements at their explicit poses, transports parity through folds, and is
// finally sampled on the target tangent plane.
[[nodiscard]] BeamFollowingFieldResult sampleBeamFollowingField(
    const scene::BenchScene& bench,
    const scene::BeamState& terminalBeam,
    std::span<const scene::BenchPathInteraction> pathInteractions,
    const BeamFollowingFieldOptions& options,
    compute::fft::IFftBackend& fftBackend,
    std::span<const PlacedSlmSparseCommand> slmCommands = {},
    const ray::ILensPrescriptionResolver* lensPrescriptions = nullptr);

} // namespace holobench::optics::wave
