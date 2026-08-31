#include "app/HolographyUiState.hpp"

#include <utility>

namespace holobench::app::holographyui {
namespace {

[[nodiscard]] bool sameFeature(
    const holographylab::GaussianObjectFeature& lhs,
    const holographylab::GaussianObjectFeature& rhs) noexcept {
    return lhs.amplitude == rhs.amplitude
        && lhs.phaseRadians == rhs.phaseRadians
        && lhs.centerXMetres == rhs.centerXMetres
        && lhs.centerYMetres == rhs.centerYMetres
        && lhs.sigmaXMetres == rhs.sigmaXMetres
        && lhs.sigmaYMetres == rhs.sigmaYMetres;
}

[[nodiscard]] bool sameReference(
    const optics::wave::PlaneWaveParameters& lhs,
    const optics::wave::PlaneWaveParameters& rhs) noexcept {
    return lhs.amplitude == rhs.amplitude
        && lhs.directionCosineX == rhs.directionCosineX
        && lhs.directionCosineY == rhs.directionCosineY
        && lhs.phaseAtOriginRadians == rhs.phaseAtOriginRadians
        && lhs.planeZMetres == rhs.planeZMetres;
}

[[nodiscard]] bool sameResponse(
    const optics::holography::ThinHologramResponseParameters& lhs,
    const optics::holography::ThinHologramResponseParameters& rhs) noexcept {
    return lhs.amplitudeBias == rhs.amplitudeBias
        && lhs.intensityToAmplitudeGain == rhs.intensityToAmplitudeGain
        && lhs.minimumAmplitudeTransmission == rhs.minimumAmplitudeTransmission
        && lhs.maximumAmplitudeTransmission == rhs.maximumAmplitudeTransmission;
}

} // namespace

bool sameHolographyLabConfig(
    const holographylab::HolographyLabConfig& lhs,
    const holographylab::HolographyLabConfig& rhs) noexcept {
    return lhs.fieldWidth == rhs.fieldWidth
        && lhs.fieldHeight == rhs.fieldHeight
        && lhs.fieldPitchXMetres == rhs.fieldPitchXMetres
        && lhs.fieldPitchYMetres == rhs.fieldPitchYMetres
        && lhs.vacuumWavelengthsMetres == rhs.vacuumWavelengthsMetres
        && lhs.refractiveIndices == rhs.refractiveIndices
        && sameFeature(lhs.objectFeatures[0], rhs.objectFeatures[0])
        && sameFeature(lhs.objectFeatures[1], rhs.objectFeatures[1])
        && lhs.transfer.h1.objectToPlateDistanceMetres
            == rhs.transfer.h1.objectToPlateDistanceMetres
        && sameReference(
            lhs.transfer.h1.recordingReference,
            rhs.transfer.h1.recordingReference)
        && sameResponse(lhs.transfer.h1.response, rhs.transfer.h1.response)
        && lhs.transfer.h2AxialPositionMetres
            == rhs.transfer.h2AxialPositionMetres
        && lhs.transfer.transplaneToleranceMetres
            == rhs.transfer.transplaneToleranceMetres
        && sameReference(
            lhs.transfer.h2RecordingReference,
            rhs.transfer.h2RecordingReference)
        && sameResponse(lhs.transfer.h2Response, rhs.transfer.h2Response);
}

HolographyUiState::HolographyUiState()
    : HolographyUiState(holographylab::makeDefaultHolographyLabConfig()) {}

HolographyUiState::HolographyUiState(
    holographylab::HolographyLabConfig initialConfig)
    : draftConfig_(std::move(initialConfig))
    , appliedConfig_(draftConfig_) {
    holographylab::validateHolographyLabConfig(draftConfig_);
}

bool HolographyUiState::isDirty() const noexcept {
    return !sameHolographyLabConfig(draftConfig_, appliedConfig_);
}

void HolographyUiState::setDraftConfig(
    const holographylab::HolographyLabConfig& config) {
    draftConfig_ = config;
}

void HolographyUiState::replaceDraftProject(
    holographylab::HolographyLabConfig config) {
    holographylab::validateHolographyLabConfig(config);
    draftConfig_ = std::move(config);
    displayedChannel_ = 1U;
}

void HolographyUiState::apply() {
    holographylab::validateHolographyLabConfig(draftConfig_);
    appliedConfig_ = draftConfig_;
    simulationRequested_ = true;
}

void HolographyUiState::requestSimulation() noexcept {
    simulationRequested_ = true;
}

void HolographyUiState::simulationSucceeded() noexcept {
    visualizationRequested_ = true;
}

void HolographyUiState::setDisplayPlane(DisplayPlane plane) noexcept {
    if (plane != displayPlane_) {
        displayPlane_ = plane;
        visualizationRequested_ = true;
    }
}

void HolographyUiState::setDisplayedChannel(std::size_t channel) noexcept {
    const std::size_t clamped = channel < 3U ? channel : 2U;
    if (clamped != displayedChannel_) {
        displayedChannel_ = clamped;
        visualizationRequested_ = true;
    }
}

bool HolographyUiState::consumeSimulationRequest() noexcept {
    return std::exchange(simulationRequested_, false);
}

bool HolographyUiState::consumeVisualizationRequest() noexcept {
    return std::exchange(visualizationRequested_, false);
}

} // namespace holobench::app::holographyui
