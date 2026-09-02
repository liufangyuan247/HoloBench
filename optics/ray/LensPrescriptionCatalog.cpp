#include "optics/ray/LensPrescriptionCatalog.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <utility>

namespace holobench::optics::ray {
namespace {

void validateProvenance(const LensPrescriptionAssetProvenance& provenance) {
    if (provenance.formatVersion <= 0 || provenance.source.empty()) {
        throw std::invalid_argument(
            "lens prescription asset provenance is incomplete");
    }
    if (provenance.contentSha256.size() != 64U
        || !std::all_of(
            provenance.contentSha256.begin(),
            provenance.contentSha256.end(),
            [](char value) {
                return (value >= '0' && value <= '9')
                    || (value >= 'a' && value <= 'f');
            })) {
        throw std::invalid_argument(
            "lens prescription asset SHA-256 must be lowercase hexadecimal");
    }
}

} // namespace

LensPrescriptionCatalog::LensPrescriptionCatalog(
    std::vector<SequentialLensPrescription> prescriptions) {
    for (auto& prescription : prescriptions) {
        registerPrescription(std::move(prescription));
    }
}

void LensPrescriptionCatalog::registerPrescription(
    SequentialLensPrescription prescription,
    std::optional<LensPrescriptionAssetProvenance> provenance) {
    validateSequentialLensPrescription(prescription);
    if (provenance.has_value()) validateProvenance(*provenance);
    const auto found = std::lower_bound(
        prescriptions_.begin(), prescriptions_.end(), prescription.id,
        [](const SequentialLensPrescription& candidate,
           const std::string& id) {
            return candidate.id < id;
        });
    if (found != prescriptions_.end() && found->id == prescription.id) {
        const auto index = static_cast<std::size_t>(
            std::distance(prescriptions_.begin(), found));
        if (*found != prescription || provenances_[index] != provenance) {
            throw std::invalid_argument(
                "lens prescription ID already names different immutable content or provenance: "
                + prescription.id);
        }
    } else {
        const auto index = static_cast<std::size_t>(
            std::distance(prescriptions_.begin(), found));
        prescriptions_.insert(found, std::move(prescription));
        provenances_.insert(
            provenances_.begin() + static_cast<std::ptrdiff_t>(index),
            std::move(provenance));
    }
}

const SequentialLensPrescription* LensPrescriptionCatalog::resolve(
    std::string_view prescriptionId) const noexcept {
    const auto found = std::lower_bound(
        prescriptions_.begin(), prescriptions_.end(), prescriptionId,
        [](const SequentialLensPrescription& candidate,
           std::string_view id) {
            return candidate.id < id;
        });
    return found != prescriptions_.end() && found->id == prescriptionId
        ? &*found
        : nullptr;
}

const LensPrescriptionAssetProvenance*
LensPrescriptionCatalog::provenance(
    std::string_view prescriptionId) const noexcept {
    const auto found = std::lower_bound(
        prescriptions_.begin(), prescriptions_.end(), prescriptionId,
        [](const SequentialLensPrescription& candidate,
           std::string_view id) {
            return candidate.id < id;
        });
    if (found == prescriptions_.end() || found->id != prescriptionId) {
        return nullptr;
    }
    const auto index = static_cast<std::size_t>(
        std::distance(prescriptions_.begin(), found));
    return provenances_[index].has_value()
        ? &*provenances_[index]
        : nullptr;
}

const std::vector<SequentialLensPrescription>&
LensPrescriptionCatalog::entries() const noexcept {
    return prescriptions_;
}

SequentialLensPrescription placeLensPrescription(
    const SequentialLensPrescription& prescription,
    const math::RigidTransform3d& firstSurfaceFrame) {
    validateSequentialLensPrescription(prescription);
    math::validateRigidTransform(firstSurfaceFrame);
    const auto& sourceAnchor = prescription.surfaces.front().localToWorld;
    SequentialLensPrescription result = prescription;
    for (auto& surface : result.surfaces) {
        const math::RigidTransform3d relative {
            .translationMetres = math::transformPointWorldToLocal(
                sourceAnchor, surface.localToWorld.translationMetres),
            .localXAxisInWorld = math::transformDirectionWorldToLocal(
                sourceAnchor, surface.localToWorld.localXAxisInWorld),
            .localYAxisInWorld = math::transformDirectionWorldToLocal(
                sourceAnchor, surface.localToWorld.localYAxisInWorld),
            .localZAxisInWorld = math::transformDirectionWorldToLocal(
                sourceAnchor, surface.localToWorld.localZAxisInWorld),
        };
        math::validateRigidTransform(relative);
        surface.localToWorld = {
            .translationMetres = math::transformPointLocalToWorld(
                firstSurfaceFrame, relative.translationMetres),
            .localXAxisInWorld = math::transformDirectionLocalToWorld(
                firstSurfaceFrame, relative.localXAxisInWorld),
            .localYAxisInWorld = math::transformDirectionLocalToWorld(
                firstSurfaceFrame, relative.localYAxisInWorld),
            .localZAxisInWorld = math::transformDirectionLocalToWorld(
                firstSurfaceFrame, relative.localZAxisInWorld),
        };
    }
    validateSequentialLensPrescription(result);
    return result;
}

SequentialLensPrescription makeDefaultNBk7BiconvexPrescription() {
    return {
        .id = "default_n_bk7_biconvex",
        .materials = {
            material::makeVacuumMaterial(),
            material::makeSchottNBk7Material(),
        },
        .surfaces = {
            {
                .id = "front",
                .geometry = {
                    .curvaturePerMetre = 20.0,
                    .conicConstant = 0.0,
                    .evenAsphereTerms = {},
                    .clearSemiDiameterMetres = 0.01,
                },
                .localToWorld = {},
                .materialBeforeId = "vacuum",
                .materialAfterId = "schott_n_bk7",
            },
            {
                .id = "rear",
                .geometry = {
                    .curvaturePerMetre = -20.0,
                    .conicConstant = 0.0,
                    .evenAsphereTerms = {},
                    .clearSemiDiameterMetres = 0.01,
                },
                .localToWorld = {
                    .translationMetres = {0.0, 0.0, 0.006}},
                .materialBeforeId = "schott_n_bk7",
                .materialAfterId = "vacuum",
            },
        },
    };
}

} // namespace holobench::optics::ray
