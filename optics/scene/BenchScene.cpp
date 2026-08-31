#include "optics/scene/BenchScene.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <set>
#include <unordered_set>
#include <utility>

namespace holobench::optics::scene {
namespace {

constexpr double kPowerTolerance = 1e-12;

void requireFinitePositive(double value, const char* field) {
    if (!std::isfinite(value) || value <= 0.0) {
        throw std::invalid_argument(std::string(field) + " must be finite and positive");
    }
}

void requireFiniteNonNegative(double value, const char* field) {
    if (!std::isfinite(value) || value < 0.0) {
        throw std::invalid_argument(std::string(field) + " must be finite and non-negative");
    }
}

void requireRasterSize(std::size_t value, const char* field) {
    if (value == 0 || value > 65536) {
        throw std::invalid_argument(std::string(field) + " must be in [1, 65536]");
    }
}

void validateSpectralChannel(const SpectralChannel& channel) {
    requireFinitePositive(channel.wavelengthMetres, "spectral wavelength_m");
    requireFiniteNonNegative(channel.powerWatts, "spectral power_w");
    if (!isStableBenchId(channel.coherenceId)) {
        throw std::invalid_argument("spectral coherence ID is invalid");
    }
}

template<typename T>
void requireParameterType(const BenchComponent& component) {
    if (!std::holds_alternative<T>(component.parameters)) {
        throw std::invalid_argument("component kind does not match its parameter type");
    }
}

void validateParameters(const LaserSourceParameters& value) {
    requireFinitePositive(value.beamRadiusMetres, "laser beam radius_m");
    if (value.channels.empty()) {
        throw std::invalid_argument("laser must contain at least one spectral channel");
    }
    std::set<double> wavelengths;
    for (const auto& channel : value.channels) {
        validateSpectralChannel(channel);
        if (!wavelengths.insert(channel.wavelengthMetres).second) {
            throw std::invalid_argument("laser spectral wavelengths must be unique");
        }
    }
}

void validateParameters(const ObjectWavefrontSourceParameters& value) {
    validateSpectralChannel(value.channel);
    requireFinitePositive(value.widthMetres, "object source width_m");
    requireFinitePositive(value.heightMetres, "object source height_m");
}

void validateParameters(const PlanarMirrorParameters& value) {
    requireFinitePositive(value.widthMetres, "mirror width_m");
    requireFinitePositive(value.heightMetres, "mirror height_m");
    requireFiniteNonNegative(value.powerReflectivity, "mirror power reflectivity");
    if (value.powerReflectivity > 1.0) {
        throw std::invalid_argument("mirror power reflectivity must not exceed one");
    }
}

void validateParameters(const BeamSplitterParameters& value) {
    requireFinitePositive(value.widthMetres, "splitter width_m");
    requireFinitePositive(value.heightMetres, "splitter height_m");
    requireFiniteNonNegative(value.powerReflectivity, "splitter power reflectivity");
    requireFiniteNonNegative(value.powerTransmissivity, "splitter power transmissivity");
    if (value.powerReflectivity + value.powerTransmissivity > 1.0 + kPowerTolerance) {
        throw std::invalid_argument("splitter power coefficients must not create energy");
    }
}

void validateParameters(const IdealThinLensParameters& value) {
    if (!std::isfinite(value.focalLengthMetres) || value.focalLengthMetres == 0.0) {
        throw std::invalid_argument("thin-lens focal length_m must be finite and non-zero");
    }
    requireFinitePositive(value.clearApertureDiameterMetres, "thin-lens aperture diameter_m");
}

void validateParameters(const RealLensAssemblyParameters& value) {
    if (!isStableBenchId(value.prescriptionId)) {
        throw std::invalid_argument("real-lens prescription ID is invalid");
    }
    requireFinitePositive(value.clearApertureDiameterMetres, "real-lens aperture diameter_m");
}

void validateParameters(const ApertureParameters& value) {
    requireFinitePositive(value.widthMetres, "aperture width_m");
    requireFinitePositive(value.heightMetres, "aperture height_m");
}

void validateParameters(const SpatialFilterParameters& value) {
    requireFinitePositive(value.focalLengthMetres, "spatial-filter focal length_m");
    requireFinitePositive(value.pinholeDiameterMetres, "spatial-filter pinhole diameter_m");
    requireFinitePositive(value.clearApertureDiameterMetres, "spatial-filter aperture diameter_m");
    if (value.pinholeDiameterMetres >= value.clearApertureDiameterMetres) {
        throw std::invalid_argument("spatial-filter pinhole must be smaller than its clear aperture");
    }
}

void validateParameters(const SpatialLightModulatorParameters& value) {
    requireFinitePositive(value.widthMetres, "SLM width_m");
    requireFinitePositive(value.heightMetres, "SLM height_m");
    requireRasterSize(value.pixelWidth, "SLM pixel width");
    requireRasterSize(value.pixelHeight, "SLM pixel height");
    if (!std::isfinite(value.fillFactor) || value.fillFactor <= 0.0 || value.fillFactor > 1.0) {
        throw std::invalid_argument("SLM fill factor must be in (0, 1]");
    }
}

void validateParameters(const ScreenDetectorParameters& value) {
    requireFinitePositive(value.widthMetres, "screen width_m");
    requireFinitePositive(value.heightMetres, "screen height_m");
    requireRasterSize(value.sampleWidth, "screen sample width");
    requireRasterSize(value.sampleHeight, "screen sample height");
}

void validateParameters(const FieldProbeParameters& value) {
    requireFinitePositive(value.widthMetres, "probe width_m");
    requireFinitePositive(value.heightMetres, "probe height_m");
    requireRasterSize(value.sampleWidth, "probe sample width");
    requireRasterSize(value.sampleHeight, "probe sample height");
}

void validateParameters(const HolographicPlateParameters& value) {
    requireFinitePositive(value.widthMetres, "plate width_m");
    requireFinitePositive(value.heightMetres, "plate height_m");
    requireFinitePositive(value.thicknessMetres, "plate thickness_m");
}

} // namespace

std::string_view benchComponentKindName(BenchComponentKind kind) noexcept {
    switch (kind) {
    case BenchComponentKind::LaserSource: return "laser_source";
    case BenchComponentKind::ObjectWavefrontSource: return "object_wavefront_source";
    case BenchComponentKind::PlanarMirror: return "planar_mirror";
    case BenchComponentKind::BeamSplitterCombiner: return "beam_splitter_combiner";
    case BenchComponentKind::IdealThinLens: return "ideal_thin_lens";
    case BenchComponentKind::RealLensAssembly: return "real_lens_assembly";
    case BenchComponentKind::Aperture: return "aperture";
    case BenchComponentKind::SpatialFilter: return "spatial_filter";
    case BenchComponentKind::SpatialLightModulator: return "spatial_light_modulator";
    case BenchComponentKind::ScreenDetector: return "screen_detector";
    case BenchComponentKind::FieldProbe: return "field_probe";
    case BenchComponentKind::HolographicPlate: return "holographic_plate";
    }
    return "unknown";
}

std::string_view benchComponentDisplayName(BenchComponentKind kind) noexcept {
    switch (kind) {
    case BenchComponentKind::LaserSource: return "Laser Source";
    case BenchComponentKind::ObjectWavefrontSource: return "Object / Wavefront Source";
    case BenchComponentKind::PlanarMirror: return "Planar Mirror";
    case BenchComponentKind::BeamSplitterCombiner: return "Beam Splitter / Combiner";
    case BenchComponentKind::IdealThinLens: return "Ideal Thin Lens";
    case BenchComponentKind::RealLensAssembly: return "Real Lens Assembly";
    case BenchComponentKind::Aperture: return "Aperture";
    case BenchComponentKind::SpatialFilter: return "Spatial Filter / Pinhole";
    case BenchComponentKind::SpatialLightModulator: return "SLM";
    case BenchComponentKind::ScreenDetector: return "Screen / Detector";
    case BenchComponentKind::FieldProbe: return "Field Probe";
    case BenchComponentKind::HolographicPlate: return "Holographic Plate";
    }
    return "Unknown";
}

BenchComponentKind benchComponentKindFromName(std::string_view name) {
    for (const auto kind : requiredBenchComponentKinds()) {
        if (benchComponentKindName(kind) == name) {
            return kind;
        }
    }
    throw std::invalid_argument("unsupported bench component kind: " + std::string(name));
}

const std::vector<BenchComponentKind>& requiredBenchComponentKinds() noexcept {
    static const std::vector<BenchComponentKind> kinds {
        BenchComponentKind::LaserSource,
        BenchComponentKind::ObjectWavefrontSource,
        BenchComponentKind::PlanarMirror,
        BenchComponentKind::BeamSplitterCombiner,
        BenchComponentKind::IdealThinLens,
        BenchComponentKind::RealLensAssembly,
        BenchComponentKind::Aperture,
        BenchComponentKind::SpatialFilter,
        BenchComponentKind::SpatialLightModulator,
        BenchComponentKind::ScreenDetector,
        BenchComponentKind::FieldProbe,
        BenchComponentKind::HolographicPlate,
    };
    return kinds;
}

bool isStableBenchId(std::string_view id) noexcept {
    if (id.empty() || id.size() > 128) {
        return false;
    }
    const auto isAsciiAlphaNumeric = [](char value) {
        return (value >= 'a' && value <= 'z')
            || (value >= 'A' && value <= 'Z')
            || (value >= '0' && value <= '9');
    };
    if (!isAsciiAlphaNumeric(id.front()) || !isAsciiAlphaNumeric(id.back())) {
        return false;
    }
    for (const char value : id) {
        if (!isAsciiAlphaNumeric(value) && value != '-' && value != '_' && value != '.') {
            return false;
        }
    }
    return true;
}

void validateBenchComponent(const BenchComponent& component) {
    if (!isStableBenchId(component.id)) {
        throw std::invalid_argument("bench component ID is invalid");
    }
    math::validateRigidTransform(component.transform);
    switch (component.kind) {
    case BenchComponentKind::LaserSource:
        requireParameterType<LaserSourceParameters>(component);
        break;
    case BenchComponentKind::ObjectWavefrontSource:
        requireParameterType<ObjectWavefrontSourceParameters>(component);
        break;
    case BenchComponentKind::PlanarMirror:
        requireParameterType<PlanarMirrorParameters>(component);
        break;
    case BenchComponentKind::BeamSplitterCombiner:
        requireParameterType<BeamSplitterParameters>(component);
        break;
    case BenchComponentKind::IdealThinLens:
        requireParameterType<IdealThinLensParameters>(component);
        break;
    case BenchComponentKind::RealLensAssembly:
        requireParameterType<RealLensAssemblyParameters>(component);
        break;
    case BenchComponentKind::Aperture:
        requireParameterType<ApertureParameters>(component);
        break;
    case BenchComponentKind::SpatialFilter:
        requireParameterType<SpatialFilterParameters>(component);
        break;
    case BenchComponentKind::SpatialLightModulator:
        requireParameterType<SpatialLightModulatorParameters>(component);
        break;
    case BenchComponentKind::ScreenDetector:
        requireParameterType<ScreenDetectorParameters>(component);
        break;
    case BenchComponentKind::FieldProbe:
        requireParameterType<FieldProbeParameters>(component);
        break;
    case BenchComponentKind::HolographicPlate:
        requireParameterType<HolographicPlateParameters>(component);
        break;
    }
    std::visit([](const auto& parameters) { validateParameters(parameters); }, component.parameters);
}

BenchComponent makeDefaultBenchComponent(BenchComponentKind kind, std::string id) {
    BenchComponent result {.id = std::move(id), .kind = kind};
    switch (kind) {
    case BenchComponentKind::LaserSource: result.parameters = LaserSourceParameters {}; break;
    case BenchComponentKind::ObjectWavefrontSource: result.parameters = ObjectWavefrontSourceParameters {}; break;
    case BenchComponentKind::PlanarMirror: result.parameters = PlanarMirrorParameters {}; break;
    case BenchComponentKind::BeamSplitterCombiner: result.parameters = BeamSplitterParameters {}; break;
    case BenchComponentKind::IdealThinLens: result.parameters = IdealThinLensParameters {}; break;
    case BenchComponentKind::RealLensAssembly: result.parameters = RealLensAssemblyParameters {}; break;
    case BenchComponentKind::Aperture: result.parameters = ApertureParameters {}; break;
    case BenchComponentKind::SpatialFilter: result.parameters = SpatialFilterParameters {}; break;
    case BenchComponentKind::SpatialLightModulator: result.parameters = SpatialLightModulatorParameters {}; break;
    case BenchComponentKind::ScreenDetector: result.parameters = ScreenDetectorParameters {}; break;
    case BenchComponentKind::FieldProbe: result.parameters = FieldProbeParameters {}; break;
    case BenchComponentKind::HolographicPlate: result.parameters = HolographicPlateParameters {}; break;
    }
    validateBenchComponent(result);
    return result;
}

BenchScene::BenchScene(std::vector<BenchComponent> components, SceneRevision revision)
    : components_(std::move(components)), revision_(revision) {
    std::unordered_set<std::string> ids;
    for (const auto& component : components_) {
        validateBenchComponent(component);
        if (!ids.insert(component.id).second) {
            throw std::invalid_argument("duplicate bench component ID: " + component.id);
        }
    }
}

SceneRevision BenchScene::revision() const noexcept {
    return revision_;
}

const std::vector<BenchComponent>& BenchScene::components() const noexcept {
    return components_;
}

const BenchComponent* BenchScene::find(std::string_view id) const noexcept {
    const auto found = std::find_if(components_.begin(), components_.end(), [id](const auto& component) {
        return component.id == id;
    });
    return found == components_.end() ? nullptr : &*found;
}

void BenchScene::add(BenchComponent component) {
    validateBenchComponent(component);
    if (find(component.id) != nullptr) {
        throw std::invalid_argument("duplicate bench component ID: " + component.id);
    }
    components_.push_back(std::move(component));
    advanceRevision();
}

void BenchScene::replace(std::string_view id, BenchComponent component) {
    validateBenchComponent(component);
    if (component.id != id) {
        throw std::invalid_argument("replace cannot change a stable component ID");
    }
    auto found = std::find_if(components_.begin(), components_.end(), [id](const auto& candidate) {
        return candidate.id == id;
    });
    if (found == components_.end()) {
        throw std::out_of_range("bench component not found: " + std::string(id));
    }
    *found = std::move(component);
    advanceRevision();
}

bool BenchScene::remove(std::string_view id) {
    const auto found = std::find_if(components_.begin(), components_.end(), [id](const auto& component) {
        return component.id == id;
    });
    if (found == components_.end()) {
        return false;
    }
    components_.erase(found);
    advanceRevision();
    return true;
}

void BenchScene::duplicate(std::string_view sourceId, std::string newId) {
    const auto* source = find(sourceId);
    if (source == nullptr) {
        throw std::out_of_range("bench component not found: " + std::string(sourceId));
    }
    BenchComponent copy = *source;
    copy.id = std::move(newId);
    add(std::move(copy));
}

void BenchScene::advanceRevision() {
    if (revision_ == std::numeric_limits<SceneRevision>::max()) {
        throw std::overflow_error("bench scene revision exhausted");
    }
    ++revision_;
}

bool BenchObservation::isStaleFor(const BenchScene& scene) const noexcept {
    return sourceRevision != scene.revision()
        || scene.find(observerComponentId) == nullptr;
}

} // namespace holobench::optics::scene
