#pragma once

#include <array>
#include <cstddef>

#include "app/HolographyReconstructionPipeline.hpp"
#include "optics/holography/VolumeHologram.hpp"

namespace holobench::compute::fft {
class IFftBackend;
}

namespace holobench::app::holographylab {

struct GaussianObjectFeature final {
    double amplitude = 0.0;
    double phaseRadians = 0.0;
    double centerXMetres = 0.0;
    double centerYMetres = 0.0;
    double sigmaXMetres = 20e-6;
    double sigmaYMetres = 20e-6;
};

struct HolographyLabConfig final {
    std::size_t fieldWidth = 32;
    std::size_t fieldHeight = 32;
    double fieldPitchXMetres = 8e-6;
    double fieldPitchYMetres = 8e-6;
    std::array<double, 3> vacuumWavelengthsMetres {638e-9, 532e-9, 450e-9};
    std::array<double, 3> refractiveIndices {1.0, 1.0, 1.0};
    std::array<GaussianObjectFeature, 2> objectFeatures {
        GaussianObjectFeature {
            .amplitude = 0.35,
            .phaseRadians = 0.2,
            .centerXMetres = 20e-6,
            .centerYMetres = -12e-6,
            .sigmaXMetres = 24e-6,
            .sigmaYMetres = 18e-6,
        },
        GaussianObjectFeature {
            .amplitude = 0.2,
            .phaseRadians = -0.4,
            .centerXMetres = -28e-6,
            .centerYMetres = 20e-6,
            .sigmaXMetres = 18e-6,
            .sigmaYMetres = 26e-6,
        },
    };
    holography::H1H2TransferConfig transfer;
    optics::holography::VolumeHologramParameters volume;
};

struct HolographyLabResult final {
    holography::RgbH1H2TransferResult rgbTransfer;
    optics::holography::VolumeHologramResult volume;
};

[[nodiscard]] HolographyLabConfig makeDefaultHolographyLabConfig();
void validateHolographyLabConfig(const HolographyLabConfig& config);

[[nodiscard]] HolographyLabResult runHolographyLab(
    const HolographyLabConfig& config,
    compute::fft::IFftBackend& fftBackend);

} // namespace holobench::app::holographylab
