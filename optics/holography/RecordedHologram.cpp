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
#include "compute/propagation/AngularSpectrumPropagator.hpp"
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
    const bool isTransmission = a.material.geometry == VolumeHologramGeometry::Transmission;
    Json result = {
        {"format", 1},
        {"model", isTransmission ? "scalar-equivalent-symmetric-transmission-v1"
                                 : "scalar-equivalent-symmetric-reflection-v1"},
        {"geometry", isTransmission ? "transmission" : "reflection"},
        {"source_plate", a.sourcePlateId},
        {"grating", vecJson(a.gratingVector)},
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
    const std::string model = p.value("model", "");
    if (p.size() < 10U || p.at("format") != 1 ||
        (model != "scalar-equivalent-symmetric-reflection-v1" &&
         model != "scalar-equivalent-symmetric-transmission-v1" &&
         model != "scalar-equivalent-symmetric-volume-v1"))
        throw std::invalid_argument("Unsupported recorded wavelength channel");
    const auto w = p.at("width").get<std::size_t>(), h = p.at("height").get<std::size_t>();
    if (w == 0U || h == 0U || w > maximumSamples / h || p.at("samples").size() != w * h)
        throw std::invalid_argument("Invalid bounded hologram dimensions");
    const auto &m = p.at("material");
    if (m.size() != 6U || p.at("centre").size() != 2U || p.at("pitch").size() != 2U)
        throw std::invalid_argument("Invalid hologram material or sampling");
    VolumeHologramParameters material;
    material.geometry = (model == "scalar-equivalent-symmetric-transmission-v1" ||
                         p.value("geometry", "") == "transmission")
                            ? VolumeHologramGeometry::Transmission
                            : VolumeHologramGeometry::Reflection;
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
    if (o.localDirection.z * r.localDirection.z < 0 && (std::abs(r.localDirection.x) > 0.01 || std::abs(r.localDirection.y) > 0.01)) {
        requested.demodulateCarrier = true;
    }
    const auto size = [&](double extent, double od, double rd, std::size_t minimum) {
        const double frequency = requested.demodulateCarrier
                                 ? (std::abs(od) / o.beam.wavelengthMetres)
                                 : (std::max({std::abs(od), std::abs(rd), std::abs(od - rd)})
                                    / o.beam.wavelengthMetres);
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
    const bool isTransmission = a.material.geometry == VolumeHologramGeometry::Transmission;
    const bool isReflection = a.material.geometry == VolumeHologramGeometry::Reflection;
    if (!isTransmission && !isReflection)
        throw std::invalid_argument("Unsupported hologram geometry");
    if (a.sourcePlateId.empty() || a.sourcePlateId.size() > 256U ||
        !math::isFinite(a.gratingVector) || math::lengthSquared(a.gratingVector) <= 0.0 ||
        !std::isfinite(a.centreXMetres) || !std::isfinite(a.centreYMetres) ||
        !std::isfinite(a.suggestedFocusDepthMetres) || a.suggestedFocusDepthMetres < 0.0 ||
        a.coupling.sampleCount() > maximumSamples || a.coupling.refractiveIndex() != 1.0 ||
        a.coupling.vacuumWavelengthMetres() != a.material.recordingVacuumWavelengthMetres)
        throw std::invalid_argument("Invalid detached volume recording");
    static_cast<void>(evaluateVolumeHologram(a.material));
    for (const auto &s : a.coupling.samples())
        if (!std::isfinite(s.real()) || !std::isfinite(s.imag()))
            throw std::invalid_argument("Non-finite recorded coupling");
    const double norm = field::computeIntegratedIntensity(a.coupling);
    if (!std::isfinite(norm) || std::abs(norm - 1.0) > 1e-6)
        throw std::invalid_argument("Recorded coupling must have unit integrated intensity");
}

RecordedHologram freezePlateRecording(const scene::BenchScene &bench,
                                      const VolumePlateRecordingResult &r) {
    if (r.isStaleFor(bench) ||
        (r.pair.geometry != PlateRecordingGeometry::Reflection &&
         r.pair.geometry != PlateRecordingGeometry::Transmission) ||
        !r.objectIncident || !r.referenceIncident)
        throw std::invalid_argument(
            "Record a current plate with retained complex fields first");
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
        o.diagnostics.sampledCentreXMetres,
        o.diagnostics.sampledCentreYMetres,
        std::move(coupling)};
    asset.suggestedFocusDepthMetres = r.nominalObjectDepthMetres;
    validateRecordedHologram(asset);
    return asset;
}

std::vector<LocalVolumeGratingSample>
computeLocalVolumeGratingField(const RecordedHologram &a) {
    const auto &f = a.coupling;
    const std::size_t w = f.width();
    const std::size_t h = f.height();
    std::vector<LocalVolumeGratingSample> result(w * h);

    const double dx = f.pitchXMetres();
    const double dy = f.pitchYMetres();

    double maxAmp = 0.0;
    for (std::size_t i = 0; i < f.sampleCount(); ++i) {
        const double amp = std::abs(f.samples()[i]);
        if (amp > maxAmp) maxAmp = amp;
    }
    if (maxAmp <= 0.0) maxAmp = 1.0;

    for (std::size_t y = 0; y < h; ++y) {
        const std::size_t yPrev = y > 0 ? y - 1 : 0;
        const std::size_t yNext = y + 1 < h ? y + 1 : h - 1;
        const double stepY = std::max(1e-9, static_cast<double>(yNext - yPrev) * dy);

        for (std::size_t x = 0; x < w; ++x) {
            const std::size_t xPrev = x > 0 ? x - 1 : 0;
            const std::size_t xNext = x + 1 < w ? x + 1 : w - 1;
            const double stepX = std::max(1e-9, static_cast<double>(xNext - xPrev) * dx);

            const auto c = f.at(x, y);
            const auto cXPrev = f.at(xPrev, y);
            const auto cXNext = f.at(xNext, y);
            const auto cYPrev = f.at(x, yPrev);
            const auto cYNext = f.at(x, yNext);

            const double norm2 = std::norm(c) + 1e-18;
            // Phase gradients dPhi/dx and dPhi/dy via Re(c)*dIm - Im(c)*dRe
            const double dReX = (cXNext.real() - cXPrev.real()) / stepX;
            const double dImX = (cXNext.imag() - cXPrev.imag()) / stepX;
            const double dPhiX = (c.real() * dImX - c.imag() * dReX) / norm2;

            const double dReY = (cYNext.real() - cYPrev.real()) / stepY;
            const double dImY = (cYNext.imag() - cYPrev.imag()) / stepY;
            const double dPhiY = (c.real() * dImY - c.imag() * dReY) / norm2;

            // Local 3D grating vector:
            // K_x = K0_x + dPhi/dx
            // K_y = K0_y + dPhi/dy
            // K_z = K0_z
            const double Kx = a.gratingVector.x + dPhiX;
            const double Ky = a.gratingVector.y + dPhiY;
            const double Kz = a.gratingVector.z;
            const double amp = std::abs(c) / maxAmp;

            const std::size_t idx = y * w + x;
            result[idx] = LocalVolumeGratingSample{
                .amplitude = static_cast<float>(amp),
                .Kx = static_cast<float>(Kx),
                .Ky = static_cast<float>(Ky),
                .Kz = static_cast<float>(Kz),
            };
        }
    }
    return result;
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
    if (root.size() != 2U || root.at("sha256") != project::sha256Hex(p.dump()))
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
    if (size == 0U || size > 3U * maximumFileBytes)
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

struct DerivedBraggCarrier {
    math::Vec3d localDiffractedDirection{0.0, 0.0, 1.0};
    math::Vec3d localIlluminationDirection{0.0, 0.0, -1.0};
};

DerivedBraggCarrier deriveBraggCarrier(const VolumeHologramParameters &mat,
                                      const math::Vec3d &K_vec) {
    const double lambda = mat.recordingVacuumWavelengthMetres;
    const double k = tau / lambda;
    const double n0 = mat.averageRefractiveIndex;
    const double km = k * n0;
    const double K_len = math::length(K_vec);
    const bool isReflection = mat.geometry == VolumeHologramGeometry::Reflection;

    DerivedBraggCarrier result;
    if (K_len < 1e-9 || K_len >= 2.0 * km) {
        result.localDiffractedDirection = {0.0, 0.0, 1.0};
        result.localIlluminationDirection = isReflection ? math::Vec3d{0.0, 0.0, -1.0}
                                                         : math::Vec3d{0.0, 0.0, 1.0};
        return result;
    }

    const double kx = K_vec.x;
    const double ky = K_vec.y;
    const double kPerp2 = (kx * kx + ky * ky) / (k * k);

    if (kPerp2 < 0.95) {
        result.localDiffractedDirection = {0.0, 0.0, 1.0};
        const double inZSign = isReflection ? -1.0 : 1.0;
        result.localIlluminationDirection = {-kx / k, -ky / k, inZSign * std::sqrt(1.0 - kPerp2)};
        return result;
    }

    const math::Vec3d K_hat = K_vec / K_len;
    const double k_perp = std::sqrt(std::max(0.0, km * km - 0.25 * K_len * K_len));

    math::Vec3d u = math::Vec3d{0.0, 0.0, 1.0} - (K_hat.z) * K_hat;
    if (math::lengthSquared(u) < 1e-12) {
        u = math::Vec3d{1.0, 0.0, 0.0} - (K_hat.x) * K_hat;
    }
    const math::Vec3d u_hat = math::normalized(u);

    math::Vec3d k_diff = 0.5 * K_vec + k_perp * u_hat;
    math::Vec3d k_in = -0.5 * K_vec + k_perp * u_hat;
    if (k_diff.z < 0.0) {
        k_diff = 0.5 * K_vec - k_perp * u_hat;
        k_in = -0.5 * K_vec - k_perp * u_hat;
    }

    const double diffZSign = isReflection ? (K_vec.z > 0.0 ? 1.0 : -1.0) : 1.0;
    const double diffPerp2 = (k_diff.x * k_diff.x + k_diff.y * k_diff.y) / (k * k);
    if (diffPerp2 < 1.0) {
        result.localDiffractedDirection = math::Vec3d{
            k_diff.x / k,
            k_diff.y / k,
            diffZSign * std::sqrt(1.0 - diffPerp2)
        };
    } else {
        result.localDiffractedDirection = {0.0, 0.0, diffZSign};
    }

    const double inPerp2 = (k_in.x * k_in.x + k_in.y * k_in.y) / (k * k);
    if (inPerp2 < 1.0) {
        const double inZSign = isReflection ? (K_vec.z > 0.0 ? -1.0 : 1.0) : 1.0;
        result.localIlluminationDirection = math::Vec3d{
            k_in.x / k,
            k_in.y / k,
            inZSign * std::sqrt(1.0 - inPerp2)
        };
    } else {
        const double inZSign = isReflection ? (K_vec.z > 0.0 ? -1.0 : 1.0) : 1.0;
        result.localIlluminationDirection = isReflection ? math::Vec3d{0.0, 0.0, inZSign}
                                                         : math::Vec3d{0.0, 0.0, 1.0};
    }

    return result;
}

HologramView defaultHologramView(const RecordedHologram &asset) {
    validateRecordedHologram(asset);
    const auto derived = deriveBraggCarrier(asset.material, asset.gratingVector);
    const auto z = derived.localDiffractedDirection;
    const auto up = std::abs(z.y) < 0.9 ? math::Vec3d{0.0, 1.0, 0.0} : math::Vec3d{1.0, 0.0, 0.0};
    const auto x = math::normalized(math::cross(up, z)), y = math::cross(z, x);

    HologramView v;
    v.platePose.localXAxisInWorld = x;
    v.platePose.localYAxisInWorld = y;
    v.platePose.localZAxisInWorld = z;
    v.illuminationDirection =
        math::transformDirectionLocalToWorld(v.platePose, derived.localIlluminationDirection);
    v.wavelengthMetres = asset.material.recordingVacuumWavelengthMetres;
    const double window =
        std::min(static_cast<double>(asset.coupling.width()) * asset.coupling.pitchXMetres(),
                 static_cast<double>(asset.coupling.height()) * asset.coupling.pitchYMetres());
    v.focusDistanceMetres = v.eyePosition.z + asset.suggestedFocusDepthMetres;
    constexpr double shortestLambda = 400e-9;
    const double maxK = tau / std::min(shortestLambda, asset.material.recordingVacuumWavelengthMetres);
    const double pitch = std::max(asset.coupling.pitchXMetres(), asset.coupling.pitchYMetres());
    const double maxNyquistRadius = (pitch > 0.0 && maxK > 0.0 && v.focusDistanceMetres > 0.0)
        ? (0.80 * std::numbers::pi * v.focusDistanceMetres / (maxK * pitch))
        : 0.0005;
    const double maxParaxialRadius = 0.09 * std::min(v.focusDistanceMetres, v.sensorDistanceMetres);
    v.pupilRadiusMetres = std::min({0.002, 0.3 * window, maxNyquistRadius, maxParaxialRadius});
    v.pupilRadiusMetres = std::max(0.0001, v.pupilRadiusMetres);
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
    material.replayAngleInMediumRadians =
        std::clamp(std::acos(std::clamp(-math::dot(internal, g), 0.0, 1.0)), 0.0, 0.4999 * std::numbers::pi);
    const auto volume = evaluateVolumeHologram(material);
    const bool isReflection = a.material.geometry == VolumeHologramGeometry::Reflection;
    const double efficiency =
        (isReflection ? (direction.z < 0.0) : (direction.z > 0.0)) && volume.kogelnikEfficiencyEvaluated
            ? volume.kogelnik.diffractionEfficiency
            : 0.0;
    const double correction = 1.0 / (1.0 - material.isotropicLinearShrinkageFraction) - 1.0;
    const double qx = k * direction.x + a.gratingVector.x * (1.0 + correction);
    const double qy = k * direction.y + a.gratingVector.y * (1.0 + correction);
    if (qx * qx + qy * qy >= k * k)
        return {std::move(outgoing), 0.0, 0.0, 0.0, 0.0};
    const double outZSign = isReflection ? 1.0 : std::copysign(1.0, direction.z);
    const math::Vec3d localOut{
        qx / k, qy / k,
        outZSign * std::sqrt(1.0 - (qx * qx + qy * qy) / (k * k))};
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
    const double zEye = std::max(0.001, v.eyePosition.z - v.platePose.translationMetres.z);
    const auto chiefAtEye =
        v.platePose.translationMetres +
        worldOut * (zEye / worldOut.z);
    if (std::abs(v.eyePosition.x - chiefAtEye.x) >
            0.5 * static_cast<double>(f.width()) * f.pitchXMetres() ||
        std::abs(v.eyePosition.y - chiefAtEye.y) >
            0.5 * static_cast<double>(f.height()) * f.pitchYMetres())
        throw std::invalid_argument("Eye is outside the supported translated observation window; "
                                    "enlarge recording support");
    const double maxParaxialPupil = 0.09 * std::min(std::max(0.001, v.focusDistanceMetres),
                                                    std::max(0.001, v.sensorDistanceMetres));
    const double clampedPupilRadius = std::min(v.pupilRadiusMetres, maxParaxialPupil);

    checkCancel(cancelled);

    // Propagate from the hologram plate to the virtual focus plane:
    // Focus plane relative to plate is zProp = zEye - v.focusDistanceMetres.
    // When focusing on an object at depth d behind the plate (focusDistance = zEye + d),
    // zProp = -d, back-propagating to the in-focus object plane.
    const double zProp = zEye - v.focusDistanceMetres;
    compute::propagation::AngularSpectrumPropagator propagator(fft);
    if (std::abs(zProp) > 1e-9) {
        static_cast<void>(propagator.propagateInPlace(outgoing, zProp));
    }
    checkCancel(cancelled);

    // Parallax shift based on eye viewpoint:
    // An object at depth |zProp| behind the plate exhibits parallax shift relative to plate window:
    const double eyeOffsetX = v.eyePosition.x - chiefAtEye.x;
    const double eyeOffsetY = v.eyePosition.y - chiefAtEye.y;
    const double parallaxX = std::abs(zProp) * (eyeOffsetX / zEye);
    const double parallaxY = std::abs(zProp) * (eyeOffsetY / zEye);
    const int shiftPixelsX = static_cast<int>(std::lround(parallaxX / outgoing.pitchXMetres()));
    const int shiftPixelsY = static_cast<int>(std::lround(parallaxY / outgoing.pitchYMetres()));

    field::ComplexField2D shifted = outgoing;
    if (shiftPixelsX != 0 || shiftPixelsY != 0) {
        for (std::size_t y = 0; y < ny; ++y) {
            for (std::size_t x = 0; x < nx; ++x) {
                const int srcX = static_cast<int>(x) + shiftPixelsX;
                const int srcY = static_cast<int>(y) + shiftPixelsY;
                if (srcX >= 0 && srcX < static_cast<int>(nx) &&
                    srcY >= 0 && srcY < static_cast<int>(ny)) {
                    shifted.at(x, y) = outgoing.at(static_cast<std::size_t>(srcX), static_cast<std::size_t>(srcY));
                } else {
                    shifted.at(x, y) = 0.0;
                }
            }
        }
    }

    // Measure power and edge leakage:
    double edge = 0.0, total = 0.0;
    for (std::size_t y = 0; y < ny; ++y) {
        for (std::size_t x = 0; x < nx; ++x) {
            const double intensity = std::norm(shifted.at(x, y));
            total += intensity;
            if (x < nx / 16U || x >= nx - nx / 16U || y < ny / 16U || y >= ny - ny / 16U)
                edge += intensity;
        }
    }

    // Pupil collection fraction:
    // For paraxial observation, the fraction of reconstructed power collected by the observer pupil
    // is bounded by pupil area over collection cone.
    const double windowRadius = 0.5 * std::min(static_cast<double>(nx) * f.pitchXMetres(),
                                               static_cast<double>(ny) * f.pitchYMetres());
    const double effectivePupilRadius = std::min(clampedPupilRadius, windowRadius);
    const double pupilFraction = std::clamp(
        std::pow(effectivePupilRadius / std::max(0.005, windowRadius), 2.0),
        1e-4, 1.0);
    const double pupilPower = std::min(exitPower, total * pupilFraction * (exitPower > 0.0 && total > 0.0 ? (exitPower / total) : 1.0));

    // Scale sensor field to preserve pupilPower integrated intensity:
    const double currentPower = field::computeIntegratedIntensity(shifted);
    if (currentPower > 0.0 && pupilPower > 0.0) {
        const double powerNorm = std::sqrt(pupilPower / currentPower);
        for (auto &s : shifted.samples()) {
            s *= powerNorm;
        }
    }

    checkCancel(cancelled);
    return {std::move(shifted), efficiency, exitPower, pupilPower,
            total > 0.0 ? edge / total : 0.0};
}
} // namespace holobench::optics::holography
