#include "app/SlmInterferenceUiState.hpp"

#include <utility>

namespace holobench::app::slmui {
namespace {

[[nodiscard]] bool sameResponse(
    const optics::slm::CalibratedSlmResponse& lhs,
    const optics::slm::CalibratedSlmResponse& rhs) noexcept {
    const auto& leftCurves = lhs.wavelengths();
    const auto& rightCurves = rhs.wavelengths();
    if (leftCurves.size() != rightCurves.size()) {
        return false;
    }
    for (std::size_t curveIndex = 0; curveIndex < leftCurves.size(); ++curveIndex) {
        const auto& left = leftCurves[curveIndex];
        const auto& right = rightCurves[curveIndex];
        if (left.vacuumWavelengthMetres != right.vacuumWavelengthMetres
            || left.commandResponse.size() != right.commandResponse.size()) {
            return false;
        }
        for (std::size_t pointIndex = 0; pointIndex < left.commandResponse.size(); ++pointIndex) {
            const auto& leftPoint = left.commandResponse[pointIndex];
            const auto& rightPoint = right.commandResponse[pointIndex];
            if (leftPoint.normalizedCommand != rightPoint.normalizedCommand
                || leftPoint.amplitudeTransmission != rightPoint.amplitudeTransmission
                || leftPoint.unwrappedPhaseDelayRadians != rightPoint.unwrappedPhaseDelayRadians) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] bool sameOptionalResponse(
    const std::optional<optics::slm::CalibratedSlmResponse>& lhs,
    const std::optional<optics::slm::CalibratedSlmResponse>& rhs) noexcept {
    if (lhs.has_value() != rhs.has_value()) {
        return false;
    }
    return !lhs.has_value() || sameResponse(lhs.value(), rhs.value());
}

[[nodiscard]] bool sameLcd(
    const optics::slm::LcdTeachingParameters& lhs,
    const optics::slm::LcdTeachingParameters& rhs) noexcept {
    if (lhs.inputPolarizerAngleRadians != rhs.inputPolarizerAngleRadians
        || lhs.analyzerAngleRadians != rhs.analyzerAngleRadians
        || lhs.liquidCrystalFastAxisAngleRadians != rhs.liquidCrystalFastAxisAngleRadians
        || lhs.zeroCommandRetardanceRadians != rhs.zeroCommandRetardanceRadians
        || lhs.fullCommandRetardanceRadians != rhs.fullCommandRetardanceRadians
        || lhs.colorFilterPattern != rhs.colorFilterPattern
        || lhs.spectralTransmission.size() != rhs.spectralTransmission.size()) {
        return false;
    }
    for (std::size_t index = 0; index < lhs.spectralTransmission.size(); ++index) {
        const auto& left = lhs.spectralTransmission[index];
        const auto& right = rhs.spectralTransmission[index];
        if (left.vacuumWavelengthMetres != right.vacuumWavelengthMetres
            || left.redAmplitude != right.redAmplitude
            || left.greenAmplitude != right.greenAmplitude
            || left.blueAmplitude != right.blueAmplitude) {
            return false;
        }
    }
    return true;
}

} // namespace

bool sameExperimentPhysicsConfig(
    const slmexperiment::SlmInterferenceExperimentConfig& lhs,
    const slmexperiment::SlmInterferenceExperimentConfig& rhs) noexcept {
    const auto& leftSlm = lhs.slm;
    const auto& rightSlm = rhs.slm;
    const auto& leftReference = lhs.referenceBeam;
    const auto& rightReference = rhs.referenceBeam;
    const auto& leftCoherence = lhs.mutualCoherence;
    const auto& rightCoherence = rhs.mutualCoherence;
    return lhs.fieldWidth == rhs.fieldWidth
        && lhs.fieldHeight == rhs.fieldHeight
        && lhs.fieldPitchXMetres == rhs.fieldPitchXMetres
        && lhs.fieldPitchYMetres == rhs.fieldPitchYMetres
        && lhs.refractiveIndex == rhs.refractiveIndex
        && lhs.vacuumWavelengthsMetres == rhs.vacuumWavelengthsMetres
        && lhs.laserAmplitude == rhs.laserAmplitude
        && lhs.lensFocalLengthMetres == rhs.lensFocalLengthMetres
        && leftSlm.pixelColumns == rightSlm.pixelColumns
        && leftSlm.pixelRows == rightSlm.pixelRows
        && leftSlm.pixelPitchXMetres == rightSlm.pixelPitchXMetres
        && leftSlm.pixelPitchYMetres == rightSlm.pixelPitchYMetres
        && leftSlm.fillFactorX == rightSlm.fillFactorX
        && leftSlm.fillFactorY == rightSlm.fillFactorY
        && leftSlm.centerXMetres == rightSlm.centerXMetres
        && leftSlm.centerYMetres == rightSlm.centerYMetres
        && leftSlm.mode == rightSlm.mode
        && leftSlm.bitDepth == rightSlm.bitDepth
        && leftSlm.phaseOffsetRadians == rightSlm.phaseOffsetRadians
        && leftSlm.phaseRangeRadians == rightSlm.phaseRangeRadians
        && lhs.deviceResponseModel == rhs.deviceResponseModel
        && sameOptionalResponse(lhs.calibratedResponse, rhs.calibratedResponse)
        && sameLcd(lhs.lcdTeaching, rhs.lcdTeaching)
        && lhs.normalizedPixelCommands == rhs.normalizedPixelCommands
        && lhs.selectedPixelColumn == rhs.selectedPixelColumn
        && lhs.selectedPixelRow == rhs.selectedPixelRow
        && leftReference.amplitude == rightReference.amplitude
        && leftReference.directionCosineX == rightReference.directionCosineX
        && leftReference.directionCosineY == rightReference.directionCosineY
        && leftReference.phaseAtOriginRadians == rightReference.phaseAtOriginRadians
        && leftReference.planeZMetres == rightReference.planeZMetres
        && leftCoherence.zeroDelayDegree == rightCoherence.zeroDelayDegree
        && leftCoherence.opticalPathDifferenceMetres == rightCoherence.opticalPathDifferenceMetres
        && leftCoherence.coherenceLengthMetres == rightCoherence.coherenceLengthMetres
        && leftCoherence.envelope == rightCoherence.envelope;
}

SlmInterferenceUiState::SlmInterferenceUiState()
    : SlmInterferenceUiState(
        slmexperiment::makeDefaultSlmInterferenceExperimentConfig()) {}

SlmInterferenceUiState::SlmInterferenceUiState(
    slmexperiment::SlmInterferenceExperimentConfig initialConfig)
    : draftConfig_(std::move(initialConfig))
    , appliedConfig_(draftConfig_) {}

bool SlmInterferenceUiState::isDirty() const noexcept {
    return !sameExperimentPhysicsConfig(draftConfig_, appliedConfig_)
        || draftCalibrationSource_ != appliedCalibrationSource_;
}

void SlmInterferenceUiState::setDraftConfig(
    const slmexperiment::SlmInterferenceExperimentConfig& config) {
    draftConfig_ = config;
}

void SlmInterferenceUiState::replaceDraftProject(
    slmexperiment::SlmInterferenceExperimentConfig config,
    std::string calibrationSource) {
    slmexperiment::validateSlmInterferenceExperimentConfig(config);
    draftConfig_ = std::move(config);
    draftCalibrationSource_ = calibrationSource.empty()
        ? "No measured LUT loaded"
        : std::move(calibrationSource);
    displayedWavelengthIndex_ = 0;
}

void SlmInterferenceUiState::setCalibration(
    optics::slm::CalibratedSlmResponse response,
    std::string source) {
    draftConfig_.vacuumWavelengthsMetres.clear();
    draftConfig_.vacuumWavelengthsMetres.reserve(response.wavelengths().size());
    for (const auto& curve : response.wavelengths()) {
        draftConfig_.vacuumWavelengthsMetres.push_back(curve.vacuumWavelengthMetres);
    }
    draftConfig_.calibratedResponse = std::move(response);
    draftConfig_.deviceResponseModel = slmexperiment::SlmDeviceResponseModel::CalibratedLut;
    draftCalibrationSource_ = source.empty() ? "In-memory measured LUT" : std::move(source);
    displayedWavelengthIndex_ = 0;
}

void SlmInterferenceUiState::clearCalibration() {
    draftConfig_.calibratedResponse.reset();
    if (draftConfig_.deviceResponseModel == slmexperiment::SlmDeviceResponseModel::CalibratedLut) {
        draftConfig_.deviceResponseModel = slmexperiment::SlmDeviceResponseModel::Ideal;
    }
    draftCalibrationSource_ = "No measured LUT loaded";
}

void SlmInterferenceUiState::apply() {
    appliedConfig_ = draftConfig_;
    appliedCalibrationSource_ = draftCalibrationSource_;
    simulationRequested_ = true;
}

void SlmInterferenceUiState::requestSimulation() noexcept {
    simulationRequested_ = true;
}

void SlmInterferenceUiState::simulationSucceeded() noexcept {
    visualizationRequested_ = true;
}

void SlmInterferenceUiState::setDisplayPlane(DisplayPlane plane) noexcept {
    if (plane != displayPlane_) {
        displayPlane_ = plane;
        visualizationRequested_ = true;
    }
}

void SlmInterferenceUiState::setDisplayedWavelengthIndex(std::size_t index) noexcept {
    if (index != displayedWavelengthIndex_) {
        displayedWavelengthIndex_ = index;
        visualizationRequested_ = true;
    }
}

bool SlmInterferenceUiState::consumeSimulationRequest() noexcept {
    return std::exchange(simulationRequested_, false);
}

bool SlmInterferenceUiState::consumeVisualizationRequest() noexcept {
    return std::exchange(visualizationRequested_, false);
}

} // namespace holobench::app::slmui
