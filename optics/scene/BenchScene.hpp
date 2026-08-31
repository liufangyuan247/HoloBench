#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "core/math/RigidTransform.hpp"

namespace holobench::optics::scene {

using SceneRevision = std::uint64_t;

enum class BenchComponentKind {
    LaserSource,
    ObjectWavefrontSource,
    PlanarMirror,
    BeamSplitterCombiner,
    IdealThinLens,
    RealLensAssembly,
    Aperture,
    SpatialFilter,
    SpatialLightModulator,
    ScreenDetector,
    FieldProbe,
    HolographicPlate,
};

[[nodiscard]] std::string_view benchComponentKindName(BenchComponentKind kind) noexcept;
[[nodiscard]] std::string_view benchComponentDisplayName(BenchComponentKind kind) noexcept;
[[nodiscard]] BenchComponentKind benchComponentKindFromName(std::string_view name);
[[nodiscard]] const std::vector<BenchComponentKind>& requiredBenchComponentKinds() noexcept;

struct SpectralChannel final {
    double wavelengthMetres = 532e-9;
    double powerWatts = 1.0;
    std::string coherenceId = "laser-1";

    bool operator==(const SpectralChannel&) const = default;
};

enum class LaserBeamProfile {
    Collimated,
    Gaussian,
};

struct LaserSourceParameters final {
    LaserBeamProfile profile = LaserBeamProfile::Collimated;
    double beamRadiusMetres = 0.005;
    std::vector<SpectralChannel> channels {{}};

    bool operator==(const LaserSourceParameters&) const = default;
};

struct ObjectWavefrontSourceParameters final {
    SpectralChannel channel {};
    double widthMetres = 0.02;
    double heightMetres = 0.02;

    bool operator==(const ObjectWavefrontSourceParameters&) const = default;
};

struct PlanarMirrorParameters final {
    double widthMetres = 0.05;
    double heightMetres = 0.05;
    double powerReflectivity = 1.0;

    bool operator==(const PlanarMirrorParameters&) const = default;
};

struct BeamSplitterParameters final {
    double widthMetres = 0.05;
    double heightMetres = 0.05;
    double powerReflectivity = 0.5;
    double powerTransmissivity = 0.5;

    bool operator==(const BeamSplitterParameters&) const = default;
};

struct IdealThinLensParameters final {
    double focalLengthMetres = 0.1;
    double clearApertureDiameterMetres = 0.05;

    bool operator==(const IdealThinLensParameters&) const = default;
};

struct RealLensAssemblyParameters final {
    std::string prescriptionId = "default-singlet";
    double clearApertureDiameterMetres = 0.05;

    bool operator==(const RealLensAssemblyParameters&) const = default;
};

enum class ApertureShape {
    Circular,
    Rectangular,
};

struct ApertureParameters final {
    ApertureShape shape = ApertureShape::Circular;
    double widthMetres = 0.01;
    double heightMetres = 0.01;

    bool operator==(const ApertureParameters&) const = default;
};

struct SpatialFilterParameters final {
    double focalLengthMetres = 0.05;
    double pinholeDiameterMetres = 25e-6;
    double clearApertureDiameterMetres = 0.025;

    bool operator==(const SpatialFilterParameters&) const = default;
};

struct SpatialLightModulatorParameters final {
    double widthMetres = 0.01536;
    double heightMetres = 0.00864;
    std::size_t pixelWidth = 1920;
    std::size_t pixelHeight = 1080;
    double fillFactor = 0.93;

    bool operator==(const SpatialLightModulatorParameters&) const = default;
};

struct ScreenDetectorParameters final {
    double widthMetres = 0.1;
    double heightMetres = 0.1;
    std::size_t sampleWidth = 512;
    std::size_t sampleHeight = 512;

    bool operator==(const ScreenDetectorParameters&) const = default;
};

struct FieldProbeParameters final {
    double widthMetres = 0.02;
    double heightMetres = 0.02;
    std::size_t sampleWidth = 256;
    std::size_t sampleHeight = 256;

    bool operator==(const FieldProbeParameters&) const = default;
};

enum class HolographicPlateRole {
    H1,
    H2,
};

struct HolographicPlateParameters final {
    double widthMetres = 0.1;
    double heightMetres = 0.1;
    double thicknessMetres = 10e-6;
    HolographicPlateRole role = HolographicPlateRole::H1;

    bool operator==(const HolographicPlateParameters&) const = default;
};

using BenchComponentParameters = std::variant<
    LaserSourceParameters,
    ObjectWavefrontSourceParameters,
    PlanarMirrorParameters,
    BeamSplitterParameters,
    IdealThinLensParameters,
    RealLensAssemblyParameters,
    ApertureParameters,
    SpatialFilterParameters,
    SpatialLightModulatorParameters,
    ScreenDetectorParameters,
    FieldProbeParameters,
    HolographicPlateParameters>;

struct BenchComponent final {
    std::string id;
    BenchComponentKind kind = BenchComponentKind::LaserSource;
    math::RigidTransform3d transform {};
    BenchComponentParameters parameters = LaserSourceParameters {};

    bool operator==(const BenchComponent&) const = default;
};

[[nodiscard]] bool isStableBenchId(std::string_view id) noexcept;
void validateBenchComponent(const BenchComponent& component);
[[nodiscard]] BenchComponent makeDefaultBenchComponent(
    BenchComponentKind kind,
    std::string id);

class BenchScene final {
public:
    BenchScene() = default;
    BenchScene(std::vector<BenchComponent> components, SceneRevision revision);

    [[nodiscard]] SceneRevision revision() const noexcept;
    [[nodiscard]] const std::vector<BenchComponent>& components() const noexcept;
    [[nodiscard]] const BenchComponent* find(std::string_view id) const noexcept;

    void add(BenchComponent component);
    void replace(std::string_view id, BenchComponent component);
    [[nodiscard]] bool remove(std::string_view id);
    void duplicate(std::string_view sourceId, std::string newId);

private:
    void advanceRevision();

    std::vector<BenchComponent> components_;
    SceneRevision revision_ = 0;
};

struct BenchObservation final {
    std::string observerComponentId;
    SceneRevision sourceRevision = 0;

    [[nodiscard]] bool isStaleFor(const BenchScene& scene) const noexcept;
};

} // namespace holobench::optics::scene
