#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "core/math/RigidTransform.hpp"
#include "optics/scene/InstrumentCalibration.hpp"

namespace holobench::optics::scene {

using SceneRevision = std::uint64_t;

enum class BenchComponentKind {
    LaserSource,
    ObjectWavefrontSource,
    PlanarMirror,
    BeamSplitterCombiner,
    XCubeCombiner,
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

// UniformPlane is retained solely so projects created before format v6 keep
// their original prescribed wavefront. New bench objects use one of the three
// opaque diffuse primitives below.
enum class ObjectSourceGeometry {
    UniformPlane,
    Cube,
    Sphere,
    Tetrahedron,
};

struct ObjectWavefrontSourceParameters final {
    SpectralChannel channel {};
    ObjectSourceGeometry geometry = ObjectSourceGeometry::Cube;
    double widthMetres = 0.02;
    double heightMetres = 0.02;
    double depthMetres = 0.02;
    double primitiveYawRadians = 0.45;
    double primitivePitchRadians = -0.28;
    std::uint64_t roughnessSeed = 1U;

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

struct XCubeCombinerParameters final {
    double sizeMetres = 0.025;
    double redWavelengthMetres = 638e-9;
    double greenWavelengthMetres = 532e-9;
    double blueWavelengthMetres = 450e-9;
    double wavelengthToleranceMetres = 35e-9;

    bool operator==(const XCubeCombinerParameters&) const = default;
};

struct IdealThinLensParameters final {
    double focalLengthMetres = 0.1;
    double clearApertureDiameterMetres = 0.05;

    bool operator==(const IdealThinLensParameters&) const = default;
};

struct RealLensAssemblyParameters final {
    std::string prescriptionId = "default_n_bk7_biconvex";
    double clearApertureDiameterMetres = 0.05;

    bool operator==(const RealLensAssemblyParameters&) const = default;
};

enum class ApertureShape {
    Circular,
    Rectangular,
    DoubleSlit,
};

struct ApertureParameters final {
    ApertureShape shape = ApertureShape::Circular;
    double widthMetres = 0.01;
    double heightMetres = 0.01;
    double slitWidthMetres = 0.10e-3;
    double slitHeightMetres = 4.0e-3;
    double slitSeparationMetres = 0.50e-3;

    bool operator==(const ApertureParameters&) const = default;
};

struct SpatialFilterParameters final {
    double focalLengthMetres = 0.05;
    double pinholeDiameterMetres = 25e-6;
    double clearApertureDiameterMetres = 0.025;

    bool operator==(const SpatialFilterParameters&) const = default;
};

enum class SlmModulationMode {
    Amplitude,
    Phase,
};

enum class SlmCommandPattern {
    Uniform,
    LinearRamp,
    Checkerboard,
};

enum class SlmCommandOrigin {
    Manual,
    Automation,
};

struct SpatialLightModulatorParameters final {
    double widthMetres = 0.01536;
    double heightMetres = 0.00864;
    std::size_t pixelWidth = 1920;
    std::size_t pixelHeight = 1080;
    double fillFactor = 0.93;
    SlmModulationMode modulationMode = SlmModulationMode::Phase;
    SlmCommandPattern commandPattern = SlmCommandPattern::Uniform;
    SlmCommandOrigin commandOrigin = SlmCommandOrigin::Manual;
    std::string commandId = "manual-command";
    double primaryCommand = 0.0;
    double secondaryCommand = 1.0;
    double horizontalCycles = 0.0;
    double verticalCycles = 0.0;
    std::size_t checkerboardCellWidthPixels = 1;
    std::size_t checkerboardCellHeightPixels = 1;
    unsigned int bitDepth = 8;
    double phaseRangeRadians = 6.283185307179586476925286766559;

    bool operator==(const SpatialLightModulatorParameters&) const = default;
};

// Evaluates the persisted procedural command at one physical SLM pixel and
// applies the declared command bit-depth quantization. The result is normalized
// to [0, 1].
[[nodiscard]] double evaluateSlmNormalizedCommand(
    const SpatialLightModulatorParameters& parameters,
    std::size_t column,
    std::size_t row);

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
    XCubeCombinerParameters,
    IdealThinLensParameters,
    RealLensAssemblyParameters,
    ApertureParameters,
    SpatialFilterParameters,
    SpatialLightModulatorParameters,
    ScreenDetectorParameters,
    FieldProbeParameters,
    HolographicPlateParameters>;

// A generic post-mounted translation/tilt assembly. The component transform
// remains the resolved optical frame consumed by every solver; benchFrame and
// the constrained degrees of freedom are the persisted mechanical truth that
// regenerates it. Render meshes never participate in this relationship.
struct MechanicalAssemblyState final {
    math::RigidTransform3d benchFrame {};
    double postHeightMetres = 0.08;
    double minimumPostHeightMetres = 0.02;
    double maximumPostHeightMetres = 0.25;
    math::Vec3d stageTranslationMetres {};
    math::Vec3d minimumStageTranslationMetres {-0.025, -0.010, -0.025};
    math::Vec3d maximumStageTranslationMetres {0.025, 0.010, 0.025};
    double mountYawRadians = 0.0;
    double minimumMountYawRadians = -3.14159265358979323846;
    double maximumMountYawRadians = 3.14159265358979323846;
    double mountPitchRadians = 0.0;
    double minimumMountPitchRadians = -0.26179938779914943654;
    double maximumMountPitchRadians = 0.26179938779914943654;

    bool operator==(const MechanicalAssemblyState&) const = default;
};

struct BenchComponent final {
    std::string id;
    BenchComponentKind kind = BenchComponentKind::LaserSource;
    math::RigidTransform3d transform {};
    BenchComponentParameters parameters = LaserSourceParameters {};
    std::optional<MechanicalAssemblyState> mechanicalAssembly;
    InstrumentIdentity instrument;

    bool operator==(const BenchComponent&) const = default;
};

[[nodiscard]] bool isStableBenchId(std::string_view id) noexcept;
[[nodiscard]] InstrumentIdentity makeDefaultInstrumentIdentity(
    BenchComponentKind kind);
void validateMechanicalAssemblyState(const MechanicalAssemblyState& state);
[[nodiscard]] math::RigidTransform3d resolveMechanicalOpticalTransform(
    const MechanicalAssemblyState& state);
[[nodiscard]] MechanicalAssemblyState makeDefaultMechanicalAssembly(
    const BenchComponent& component);
void applyMechanicalAssembly(
    BenchComponent& component,
    const MechanicalAssemblyState& state);
void rebaseMechanicalAssembly(
    BenchComponent& component,
    const math::RigidTransform3d& desiredOpticalTransform);
void removeMechanicalAssembly(BenchComponent& component) noexcept;
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
