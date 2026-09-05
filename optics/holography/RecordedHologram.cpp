#include "optics/holography/RecordedHologram.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <fstream>
#include <numbers>
#include <stdexcept>
#include <vector>

#include <nlohmann/json.hpp>

#include "compute/fourier/FourierOptics.hpp"
#include "compute/propagation/TiltedPlanePropagator.hpp"
#include "core/field/FieldObservables.hpp"
#include "core/project/Sha256.hpp"
#include "optics/wave/FieldElements.hpp"

namespace holobench::optics::holography {
namespace {
constexpr double tau = 2.0 * std::numbers::pi;
constexpr std::size_t maximumSamples = 2048U * 2048U;
constexpr std::size_t maximumFileBytes = 256U * 1024U * 1024U;

void checkCancel(const std::atomic_bool *cancelled) {
    if (cancelled && cancelled->load())
        throw std::runtime_error("Observation cancelled");
}

math::Vec3d externalDirection(math::Vec3d internal, double index) {
    const double x = index * internal.x;
    const double y = index * internal.y;
    if (x * x + y * y >= 1.0)
        throw std::invalid_argument("Recording direction cannot exit into air");
    return {x, y, std::copysign(std::sqrt(1.0 - x * x - y * y), internal.z)};
}

void requireUnit(math::Vec3d value) {
    if (!math::isFinite(value) || std::abs(math::lengthSquared(value) - 1.0) > 1e-8)
        throw std::invalid_argument("Hologram directions must be finite unit vectors");
}

using Json = nlohmann::json;
Json vecJson(math::Vec3d v) { return Json::array({v.x, v.y, v.z}); }
math::Vec3d readVec(const Json &v) {
    if (!v.is_array() || v.size() != 3U)
        throw std::invalid_argument("Invalid hologram vector");
    return {v.at(0).get<double>(), v.at(1).get<double>(), v.at(2).get<double>()};
}
Json payload(const RecordedHologram &a) {
    Json samples = Json::array();
    for (const auto &s : a.coupling.samples())
        samples.push_back(Json::array({s.real(), s.imag()}));
    const auto &m = a.material;
    Json result = {
        {"format", 1},
        {"model", "scalar-equivalent-symmetric-reflection-v1"},
        {"source_plate", a.sourcePlateId},
        {"grating", vecJson(a.gratingVector)},
        {"reference", vecJson(a.referenceDirection)},
        {"object", vecJson(a.objectDirection)},
        {"centre", Json::array({a.centreXMetres, a.centreYMetres})},
        {"material",
         Json::array({m.recordedThicknessMetres, m.averageRefractiveIndex,
                      m.refractiveIndexModulation, m.recordingVacuumWavelengthMetres,
                      m.recordingBraggAngleInMediumRadians, m.isotropicLinearShrinkageFraction})},
        {"width", a.coupling.width()},
        {"height", a.coupling.height()},
        {"pitch", Json::array({a.coupling.pitchXMetres(), a.coupling.pitchYMetres()})},
        {"samples", std::move(samples)}};
    if (a.suggestedFocusDepthMetres != 0.0)
        result["focus_depth_m"] = a.suggestedFocusDepthMetres;
    return result;
}
RecordedHologram decodePayload(const Json& p) {
    if (p.size() != (p.contains("focus_depth_m") ? 13U : 12U) || p.at("format") != 1 ||
        p.at("model") != "scalar-equivalent-symmetric-reflection-v1")
        throw std::invalid_argument("Unsupported recorded wavelength channel");
    const auto w = p.at("width").get<std::size_t>(), h = p.at("height").get<std::size_t>();
    if (w == 0U || h == 0U || w > maximumSamples / h || p.at("samples").size() != w * h)
        throw std::invalid_argument("Invalid bounded hologram dimensions");
    const auto &m = p.at("material");
    if (m.size() != 6U || p.at("centre").size() != 2U || p.at("pitch").size() != 2U)
        throw std::invalid_argument("Invalid hologram material or sampling");
    VolumeHologramParameters material;
    material.recordedThicknessMetres = m.at(0).get<double>();
    material.averageRefractiveIndex = m.at(1).get<double>();
    material.refractiveIndexModulation = m.at(2).get<double>();
    material.recordingVacuumWavelengthMetres = m.at(3).get<double>();
    material.replayVacuumWavelengthMetres = material.recordingVacuumWavelengthMetres;
    material.recordingBraggAngleInMediumRadians = m.at(4).get<double>();
    material.replayAngleInMediumRadians = material.recordingBraggAngleInMediumRadians;
    material.isotropicLinearShrinkageFraction = m.at(5).get<double>();
    field::ComplexField2D coupling(w, h, p.at("pitch").at(0).get<double>(),
                                   p.at("pitch").at(1).get<double>(),
                                   material.recordingVacuumWavelengthMetres);
    for (std::size_t i = 0; i < coupling.sampleCount(); ++i) {
        const auto &s = p.at("samples").at(i);
        if (!s.is_array() || s.size() != 2U)
            throw std::invalid_argument("Invalid complex sample");
        coupling.samples()[i] = {s.at(0).get<double>(), s.at(1).get<double>()};
    }
    RecordedHologram result{p.at("source_plate").get<std::string>(),
                            material,
                            readVec(p.at("grating")),
                            readVec(p.at("reference")),
                            readVec(p.at("object")),
                            p.at("centre").at(0).get<double>(),
                            p.at("centre").at(1).get<double>(),
                            std::move(coupling)};
    result.suggestedFocusDepthMetres = p.value("focus_depth_m", 0.0);
    validateRecordedHologram(result);
    return result;
}
} // namespace

PlateFieldSamplingOptions reconstructionSampling(
    const scene::BenchScene &bench, const PlateIncidentFieldSet &fields,
    std::uint64_t objectBranch, std::uint64_t referenceBranch,
    PlateFieldSamplingOptions requested) {
    if (fields.isStaleFor(bench))
        throw std::invalid_argument("Reconstruction sampling requires current branch evidence");
    static_cast<void>(makePlateRecordingPair(fields, objectBranch, referenceBranch));
    const auto branch = [&](std::uint64_t id) -> const PlateIncidentBranch & {
        return *std::find_if(fields.branches.begin(), fields.branches.end(),
                            [id](const auto &b) { return b.beam.provenance.branchId == id; });
    };
    const auto &o = branch(objectBranch), &r = branch(referenceBranch);
    const auto &plate = std::get<scene::HolographicPlateParameters>(
        bench.find(fields.plateComponentId)->parameters);
    const double width = requested.extentWidthMetres > 0 ? requested.extentWidthMetres : plate.widthMetres;
    const double height = requested.extentHeightMetres > 0 ? requested.extentHeightMetres : plate.heightMetres;
    const auto size = [&](double extent, double od, double rd, std::size_t minimum) {
        const double frequency = std::max({std::abs(od), std::abs(rd), std::abs(od - rd)})
                                 / o.beam.wavelengthMetres;
        const double required = std::max(static_cast<double>(minimum), 2.5 * extent * frequency + 2.0);
        if (!std::isfinite(required) || required > 2048.0 || extent <= 0)
            throw std::invalid_argument("Reconstruction needs more than 2048 samples on an axis; reduce the hogel/window size or recording angle");
        std::size_t n = 32;
        while (static_cast<double>(n) < required) n *= 2;
        return n;
    };
    requested.sampleWidth = size(width, o.localDirection.x, r.localDirection.x, requested.sampleWidth);
    requested.sampleHeight = size(height, o.localDirection.y, r.localDirection.y, requested.sampleHeight);
    return requested;
}

void validateRecordedHologram(const RecordedHologram &a) {
    requireUnit(a.referenceDirection);
    requireUnit(a.objectDirection);
    if (a.sourcePlateId.empty() || a.sourcePlateId.size() > 256U ||
        a.material.geometry != VolumeHologramGeometry::Reflection ||
        !math::isFinite(a.gratingVector) || math::lengthSquared(a.gratingVector) <= 0.0 ||
        !std::isfinite(a.centreXMetres) || !std::isfinite(a.centreYMetres) ||
        !std::isfinite(a.suggestedFocusDepthMetres) || a.suggestedFocusDepthMetres < 0.0 ||
        a.referenceDirection.z * a.objectDirection.z >= 0.0 ||
        a.coupling.sampleCount() > maximumSamples || a.coupling.refractiveIndex() != 1.0 ||
        a.coupling.vacuumWavelengthMetres() != a.material.recordingVacuumWavelengthMetres)
        throw std::invalid_argument("Invalid detached reflection recording");
    static_cast<void>(evaluateVolumeHologram(a.material));
    const double index = a.material.averageRefractiveIndex;
    const auto internal = [&](math::Vec3d d) {
        return math::Vec3d{
            d.x / index, d.y / index,
            std::copysign(std::sqrt(1.0 - (d.x * d.x + d.y * d.y) / (index * index)), d.z)};
    };
    const auto expected = (internal(a.objectDirection) - internal(a.referenceDirection)) *
                          (tau * index / a.material.recordingVacuumWavelengthMetres);
    if (math::length(a.gratingVector - expected) > math::length(expected) * 1e-8)
        throw std::invalid_argument(
            "Stored grating vector disagrees with the recording directions");
    if (std::abs(a.gratingVector.x / tau) >= 0.5 / a.coupling.pitchXMetres() ||
        std::abs(a.gratingVector.y / tau) >= 0.5 / a.coupling.pitchYMetres())
        throw std::invalid_argument(
            "Recorded object-reference product exceeds the sampled carrier bandwidth");
    for (const auto &s : a.coupling.samples())
        if (!std::isfinite(s.real()) || !std::isfinite(s.imag()))
            throw std::invalid_argument("Non-finite recorded coupling");
    const double norm = field::computeIntegratedIntensity(a.coupling);
    if (!std::isfinite(norm) || std::abs(norm - 1.0) > 1e-6)
        throw std::invalid_argument("Recorded coupling must have unit integrated intensity");
}

RecordedHologram freezeReflectionRecording(const scene::BenchScene &bench,
                                           const VolumePlateRecordingResult &r) {
    if (r.isStaleFor(bench) || r.pair.geometry != PlateRecordingGeometry::Reflection ||
        !r.objectIncident || !r.referenceIncident)
        throw std::invalid_argument(
            "Record a current reflection plate with retained complex fields first");
    const auto &o = *r.objectIncident;
    const auto &ref = *r.referenceIncident;
    if (!o.diagnostics.carrierSampled || !ref.diagnostics.carrierSampled)
        throw std::invalid_argument("Recording carrier is undersampled; increase recording "
                                    "resolution before opening the showroom");
    if (o.field.width() != ref.field.width() || o.field.height() != ref.field.height() ||
        o.field.pitchXMetres() != ref.field.pitchXMetres() ||
        o.field.pitchYMetres() != ref.field.pitchYMetres() ||
        o.diagnostics.sampledCentreXMetres != ref.diagnostics.sampledCentreXMetres ||
        o.diagnostics.sampledCentreYMetres != ref.diagnostics.sampledCentreYMetres)
        throw std::invalid_argument("Recording fields must share a physical grid");
    auto coupling = o.field;
    for (std::size_t i = 0; i < coupling.sampleCount(); ++i)
        coupling.samples()[i] *= std::conj(ref.field.samples()[i]);
    const double norm = field::computeIntegratedIntensity(coupling);
    if (!std::isfinite(norm) || norm <= 0.0)
        throw std::invalid_argument("Recording has no overlapping object/reference field");
    for (auto &s : coupling.samples())
        s /= std::sqrt(norm);
    RecordedHologram asset{
        r.plateComponentId,
        r.nominalReplayParameters,
        r.recordedGratingVectorLocalRadiansPerMetre,
        externalDirection(r.referenceDirectionInMediumLocal, r.material.averageRefractiveIndex),
        externalDirection(r.objectDirectionInMediumLocal, r.material.averageRefractiveIndex),
        o.diagnostics.sampledCentreXMetres,
        o.diagnostics.sampledCentreYMetres,
        std::move(coupling)};
    asset.suggestedFocusDepthMetres = r.nominalObjectDepthMetres;
    validateRecordedHologram(asset);
    return asset;
}

void saveRecordedHologram(const RecordedHologram &asset, const std::filesystem::path &path) {
    validateRecordedHologram(asset);
    const auto data = payload(asset);
    const std::string text =
        Json{{"payload", data}, {"sha256", project::sha256Hex(data.dump())}}.dump();
    if (text.size() > maximumFileBytes)
        throw std::invalid_argument("Hologram file exceeds 256 MiB");
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    stream.close();
    if (!stream)
        throw std::runtime_error("Could not save recorded hologram");
}

RecordedHologram loadRecordedHologram(const std::filesystem::path &path) {
    const auto size = std::filesystem::file_size(path);
    if (size == 0U || size > maximumFileBytes)
        throw std::invalid_argument("Hologram file must be at most 256 MiB");
    std::ifstream stream(path, std::ios::binary);
    std::string text(static_cast<std::size_t>(size), '\0');
    stream.read(text.data(), static_cast<std::streamsize>(size));
    if (!stream)
        throw std::runtime_error("Could not read recorded hologram");
    const auto root = Json::parse(text);
    const auto &p = root.at("payload");
    if (root.size() != 2U || p.size() != (p.contains("focus_depth_m") ? 13U : 12U) || p.at("format") != 1 ||
        p.at("model") != "scalar-equivalent-symmetric-reflection-v1" ||
        root.at("sha256") != project::sha256Hex(p.dump()))
        throw std::invalid_argument("Unsupported or corrupt recorded hologram");
    return decodePayload(p);
}


void saveRecordedHolograms(const std::vector<RecordedHologram>& channels, const std::filesystem::path& path) {
    if (channels.empty() || channels.size() > 3U)
        throw std::invalid_argument("A recorded plate contains one to three independent wavelength channels");
    if (channels.size() == 1U) { saveRecordedHologram(channels.front(), path); return; }
    Json recordings = Json::array();
    for (const auto& channel : channels) {
        validateRecordedHologram(channel);
        recordings.push_back(payload(channel));
    }
    const Json data{{"format", 2}, {"channels", std::move(recordings)}};
    const std::string text = Json{{"payload", data}, {"sha256", project::sha256Hex(data.dump())}}.dump();
    if (text.size() > 3U * maximumFileBytes)
        throw std::invalid_argument("Recorded channel bundle exceeds 768 MiB");
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    file.close();
    if (!file) throw std::runtime_error("Could not save recorded channels");
}

std::vector<RecordedHologram> loadRecordedHolograms(const std::filesystem::path& path) {
    const auto size = std::filesystem::file_size(path);
    if (size == 0 || size > 3U * maximumFileBytes)
        throw std::invalid_argument("Recorded channel bundle must be at most 768 MiB");
    std::ifstream file(path, std::ios::binary);
    std::string text(static_cast<std::size_t>(size), '\0');
    file.read(text.data(), static_cast<std::streamsize>(size));
    if (!file) throw std::runtime_error("Could not read recorded channels");
    const auto root = Json::parse(text);
    const auto& data = root.at("payload");
    if (root.size() != 2U || root.at("sha256") != project::sha256Hex(data.dump()))
        throw std::invalid_argument("Corrupt recorded channel bundle");
    if (data.at("format") == 1) return {decodePayload(data)};
    if (data.at("format") != 2 || data.size() != 2U || !data.at("channels").is_array()
        || data.at("channels").empty() || data.at("channels").size() > 3U)
        throw std::invalid_argument("Unsupported recorded channel bundle");
    std::vector<RecordedHologram> channels;
    for (const auto& channel : data.at("channels")) channels.push_back(decodePayload(channel));
    return channels;
}

HologramView defaultHologramView(const RecordedHologram &asset) {
    validateRecordedHologram(asset);
    const auto z = asset.objectDirection;
    const auto up = std::abs(z.y) < 0.9 ? math::Vec3d{0, 1, 0} : math::Vec3d{1, 0, 0};
    const auto x = math::normalized(math::cross(up, z)), y = math::cross(z, x);
    HologramView v;
    v.platePose.localXAxisInWorld = {x.x, y.x, z.x};
    v.platePose.localYAxisInWorld = {x.y, y.y, z.y};
    v.platePose.localZAxisInWorld = {x.z, y.z, z.z};
    v.illuminationDirection =
        math::transformDirectionLocalToWorld(v.platePose, asset.referenceDirection);
    v.wavelengthMetres = asset.material.recordingVacuumWavelengthMetres;
    const double window =
        std::min(static_cast<double>(asset.coupling.width()) * asset.coupling.pitchXMetres(),
                 static_cast<double>(asset.coupling.height()) * asset.coupling.pitchYMetres());
    v.pupilRadiusMetres = 0.3 * window;
    v.focusDistanceMetres = v.eyePosition.z + asset.suggestedFocusDepthMetres;
    return v;
}

RecordedHologram makeTwoPointReflectionReference() {
    constexpr double wavelength = 532e-9;
    const double k = tau / wavelength;
    field::ComplexField2D coupling(256, 256, 5e-6, 5e-6, wavelength);
    for (std::size_t y = 0; y < coupling.height(); ++y)
        for (std::size_t x = 0; x < coupling.width(); ++x) {
            const double px = coupling.xCoordinateMetres(x), py = coupling.yCoordinateMetres(y);
            const double aperture = std::exp(-(px * px + py * py) / (0.00025 * 0.00025));
            coupling.at(x, y) =
                aperture *
                (std::polar(1.0, k * ((px - 0.00018) * (px - 0.00018) + py * py) / (2 * 0.04)) +
                 std::polar(1.0, k * ((px + 0.00018) * (px + 0.00018) + py * py) / (2 * 0.08)));
        }
    const double norm = std::sqrt(field::computeIntegratedIntensity(coupling));
    for (auto &s : coupling.samples())
        s /= norm;
    VolumeHologramParameters material;
    RecordedHologram result{"Analytic reference: coherent points at 40 and 80 mm depth",
                            material,
                            {0, 0, 2 * k * material.averageRefractiveIndex},
                            {0, 0, -1},
                            {0, 0, 1},
                            0,
                            0,
                            std::move(coupling)};
    validateRecordedHologram(result);
    return result;
}

HologramViewResult observeRecordedHologram(const RecordedHologram &a, const HologramView &v,
                                           compute::fft::IFftBackend &fft,
                                           const std::atomic_bool *cancelled) {
    checkCancel(cancelled);
    validateRecordedHologram(a);
    math::validateRigidTransform(v.platePose);
    requireUnit(v.illuminationDirection);
    if (!math::isFinite(v.eyePosition) || v.eyePosition.z <= v.platePose.translationMetres.z ||
        !std::isfinite(v.wavelengthMetres) || v.wavelengthMetres < 380e-9 ||
        v.wavelengthMetres > 780e-9 || !std::isfinite(v.irradianceWattsPerSquareMetre) ||
        v.irradianceWattsPerSquareMetre < 0.0 || !std::isfinite(v.pupilRadiusMetres) ||
        v.pupilRadiusMetres <= 0.0 || !std::isfinite(v.focusDistanceMetres) ||
        v.focusDistanceMetres <= 0.0 || !std::isfinite(v.sensorDistanceMetres) ||
        v.sensorDistanceMetres <= 0.0 || (v.paddingFactor != 1U && v.paddingFactor != 2U))
        throw std::invalid_argument("Invalid showroom illumination or observer");
    const auto direction =
        math::transformDirectionWorldToLocal(v.platePose, v.illuminationDirection);
    const auto &f = a.coupling;
    const auto nx = f.width() * v.paddingFactor, ny = f.height() * v.paddingFactor;
    if (nx > 2048U || ny > 2048U)
        throw std::invalid_argument("Observer window exceeds the bounded 2048 by 2048 grid");
    field::ComplexField2D outgoing(nx, ny, f.pitchXMetres(), f.pitchYMetres(), v.wavelengthMetres);
    const double k = tau / v.wavelengthMetres;
    auto material = a.material;
    const auto g = math::normalized(a.gratingVector);
    const double index = material.averageRefractiveIndex;
    const math::Vec3d internal{
        direction.x / index, direction.y / index,
        std::copysign(std::sqrt(1.0 - (direction.x * direction.x + direction.y * direction.y) /
                                          (index * index)),
                      direction.z)};
    material.replayVacuumWavelengthMetres = v.wavelengthMetres;
    material.replayAngleInMediumRadians = std::acos(std::clamp(-math::dot(internal, g), 0.0, 1.0));
    const auto volume = evaluateVolumeHologram(material);
    const double efficiency =
        direction.z * a.referenceDirection.z > 0.0 && volume.kogelnikEfficiencyEvaluated
            ? volume.kogelnik.diffractionEfficiency
            : 0.0;
    const double correction = 1.0 / (1.0 - material.isotropicLinearShrinkageFraction) - 1.0;
    const double qx = k * direction.x + a.gratingVector.x * (1.0 + correction);
    const double qy = k * direction.y + a.gratingVector.y * (1.0 + correction);
    if (qx * qx + qy * qy >= k * k)
        throw std::invalid_argument("Reconstructed order cannot exit into air");
    const math::Vec3d localOut{
        qx / k, qy / k,
        std::copysign(std::sqrt(1.0 - (qx * qx + qy * qy) / (k * k)), a.objectDirection.z)};
    const auto worldOut = math::transformDirectionLocalToWorld(v.platePose, localOut);
    if (worldOut.z <= 0.0)
        throw std::invalid_argument("Observer is behind the reconstructed hemisphere");
    const double sourceArea =
        static_cast<double>(f.sampleCount()) * f.pitchXMetres() * f.pitchYMetres();
    const double exitPower =
        v.irradianceWattsPerSquareMetre * sourceArea * std::abs(direction.z) * efficiency;
    const double scale = std::sqrt(exitPower / std::abs(localOut.z));
    for (std::size_t y = 0; y < f.height(); ++y) {
        checkCancel(cancelled);
        for (std::size_t x = 0; x < f.width(); ++x) {
            const double phase =
                (k * direction.x + a.gratingVector.x * correction) * f.xCoordinateMetres(x) +
                (k * direction.y + a.gratingVector.y * correction) * f.yCoordinateMetres(y);
            outgoing.at(x + (nx - f.width()) / 2U, y + (ny - f.height()) / 2U) =
                f.at(x, y) * scale * std::polar(1.0, std::remainder(phase, tau));
        }
    }
    auto plane = v.platePose;
    // The frozen local analysis window is centred at the showroom pivot.
    math::RigidTransform3d pupilPlane;
    pupilPlane.translationMetres = v.eyePosition;
    const double halfWindow = 0.5 * std::min(static_cast<double>(nx) * f.pitchXMetres(),
                                             static_cast<double>(ny) * f.pitchYMetres());
    const auto chiefAtEye =
        v.platePose.translationMetres +
        worldOut * ((v.eyePosition.z - v.platePose.translationMetres.z) / worldOut.z);
    if (std::abs(v.eyePosition.x - chiefAtEye.x) >
            0.5 * static_cast<double>(f.width()) * f.pitchXMetres() ||
        std::abs(v.eyePosition.y - chiefAtEye.y) >
            0.5 * static_cast<double>(f.height()) * f.pitchYMetres())
        throw std::invalid_argument("Eye is outside the supported translated observation window; "
                                    "enlarge recording support");
    if (v.pupilRadiusMetres >= halfWindow)
        throw std::invalid_argument("Observer pupil exceeds the sampled window");
    if (v.pupilRadiusMetres / v.focusDistanceMetres > 0.1 ||
        v.pupilRadiusMetres / v.sensorDistanceMetres > 0.1)
        throw std::invalid_argument("Observer exceeds the scalar paraxial camera domain");
    checkCancel(cancelled);
    compute::propagation::TiltedPlanePropagator propagator(fft);
    static_cast<void>(propagator.propagatePaddedInPlace(outgoing, plane, pupilPlane, worldOut));
    checkCancel(cancelled);
    double edge = 0.0, total = 0.0;
    for (std::size_t y = 0; y < ny; ++y)
        for (std::size_t x = 0; x < nx; ++x) {
            const double intensity = std::norm(outgoing.at(x, y));
            total += intensity;
            if (x < nx / 16U || x >= nx - nx / 16U || y < ny / 16U || y >= ny - ny / 16U)
                edge += intensity;
        }
    static_cast<void>(wave::applyCircularAperture(outgoing, {.radiusMetres = v.pupilRadiusMetres}));
    const double pupilPower = field::computeIntegratedIntensity(outgoing);
    // Lens followed by Fresnel propagation to b: quadratic pupil phase is
    // k*r^2/2*(1/b-1/f)=-k*r^2/(2*s), where 1/f=1/s+1/b.
    if (k * v.pupilRadiusMetres * std::max(f.pitchXMetres(), f.pitchYMetres()) /
            v.focusDistanceMetres >
        std::numbers::pi)
        throw std::invalid_argument("Observer focus phase is undersampled");
    static_cast<void>(
        wave::applyIdealThinLensPhase(outgoing, {.focalLengthMetres = v.focusDistanceMetres}));
    checkCancel(cancelled);
    auto sensor = compute::fourier::FourierLensTransform(fft).transformFrontToBackFocalPlane(
        outgoing, v.sensorDistanceMetres);
    checkCancel(cancelled);
    // Sensor quadratic/global phase is immaterial to this intensity camera.
    return {std::move(sensor.field), efficiency, exitPower, pupilPower,
            total > 0.0 ? edge / total : 0.0};
}
} // namespace holobench::optics::holography
