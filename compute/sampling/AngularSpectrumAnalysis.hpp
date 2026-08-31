#pragma once

#include <complex>
#include <cstddef>
#include <vector>

namespace holobench::field {
class ComplexField2D;
}

namespace holobench::compute::fft {
class IFftBackend;
}

namespace holobench::compute::sampling {

enum class AngularSpectrumBinKind {
    Propagating,
    Evanescent,
};

struct AngularSpectrumBin final {
    double frequencyXCyclesPerMetre = 0.0;
    double frequencyYCyclesPerMetre = 0.0;
    double radialFrequencyCyclesPerMetre = 0.0;
    double longitudinalFrequencyCyclesPerMetre = 0.0;
    double evanescentDecayCyclesPerMetre = 0.0;
    std::complex<double> coefficient {0.0, 0.0};
    double normalizedSpectralIntensity = 0.0;
    AngularSpectrumBinKind kind = AngularSpectrumBinKind::Propagating;
};

struct AngularSpectrumAnalysis final {
    std::size_t width = 0U;
    std::size_t height = 0U;
    double frequencyPitchXCyclesPerMetre = 0.0;
    double frequencyPitchYCyclesPerMetre = 0.0;
    double propagatingCutoffCyclesPerMetre = 0.0;
    std::size_t propagatingBinCount = 0U;
    std::size_t evanescentBinCount = 0U;
    double propagatingSpectralEnergyFraction = 0.0;
    double evanescentSpectralEnergyFraction = 0.0;
    std::vector<AngularSpectrumBin> centeredBins;

    [[nodiscard]] const AngularSpectrumBin& at(
        std::size_t centeredX,
        std::size_t centeredY) const;
};

/** Computes a centred, display-ready angular-spectrum map without mutating the field. */
[[nodiscard]] AngularSpectrumAnalysis analyzeAngularSpectrum(
    const field::ComplexField2D& field,
    fft::IFftBackend& fftBackend);

} // namespace holobench::compute::sampling
