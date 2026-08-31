#pragma once

namespace holobench::optics::holography {

enum class VolumeHologramGeometry {
    Transmission,
    Reflection,
};

// Uniform, lossless, sinusoidal phase grating for the first scalar-TE
// Kogelnik teaching model. Angles are internal medium angles. Shrinkage is
// isotropic linear shrinkage applied after recording; it changes both the
// physical thickness and the recorded grating period.
struct VolumeHologramParameters final {
    VolumeHologramGeometry geometry = VolumeHologramGeometry::Reflection;
    double recordedThicknessMetres = 20e-6;
    double averageRefractiveIndex = 1.5;
    double refractiveIndexModulation = 0.01;
    double recordingVacuumWavelengthMetres = 532e-9;
    double replayVacuumWavelengthMetres = 532e-9;
    double recordingBraggAngleInMediumRadians = 0.0;
    double replayAngleInMediumRadians = 0.0;
    double isotropicLinearShrinkageFraction = 0.0;
};

struct KogelnikEfficiencyResult final {
    double diffractionEfficiency = 0.0;
    double couplingStrength = 0.0;
    double detuningParameter = 0.0;
};

struct VolumeHologramResult final {
    double replayThicknessMetres = 0.0;
    double recordedGratingPeriodMetres = 0.0;
    double replayGratingPeriodMetres = 0.0;
    double replayWavenumberRadiansPerMetre = 0.0;
    double phaseMismatchRadiansPerMetre = 0.0;
    double diffractedInternalAngleRadians = 0.0;
    bool diffractedOrderPropagating = false;
    bool kogelnikEfficiencyEvaluated = false;
    KogelnikEfficiencyResult kogelnik;
    double exactBraggEfficiencyAtReplayCoupling = 0.0;
};

// Evaluates the canonical scalar coupled-wave result from dimensionless
// coupling nu and detuning xi. Both geometries are even in xi. At xi=0,
// transmission reduces to sin^2(nu), while reflection reduces to tanh^2(nu).
[[nodiscard]] KogelnikEfficiencyResult evaluateKogelnikEfficiency(
    VolumeHologramGeometry geometry,
    double couplingStrength,
    double detuningParameter);

// Derives the grating-vector mismatch from the recording and replay state,
// then evaluates the applicable two-wave Kogelnik solution. A transmission
// order outside the replay medium's propagating circle is reported explicitly
// and is not assigned a coupled-wave efficiency.
[[nodiscard]] VolumeHologramResult evaluateVolumeHologram(
    const VolumeHologramParameters& parameters);

} // namespace holobench::optics::holography
