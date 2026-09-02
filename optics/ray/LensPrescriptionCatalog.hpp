#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "optics/ray/SequentialLens.hpp"

namespace holobench::optics::ray {

struct LensPrescriptionAssetProvenance final {
    int formatVersion = 1;
    std::string source;
    std::string contentSha256;

    bool operator==(const LensPrescriptionAssetProvenance&) const = default;
};

// Read-only prescription lookup used by shared Bench solvers. Project files
// persist stable IDs; the resolver supplies validated runtime assets without
// making a render mesh or UI panel into optical truth.
class ILensPrescriptionResolver {
public:
    virtual ~ILensPrescriptionResolver() = default;

    [[nodiscard]] virtual const SequentialLensPrescription* resolve(
        std::string_view prescriptionId) const noexcept = 0;
};

class LensPrescriptionCatalog final : public ILensPrescriptionResolver {
public:
    LensPrescriptionCatalog() = default;
    explicit LensPrescriptionCatalog(
        std::vector<SequentialLensPrescription> prescriptions);

    // Registration is content-immutable: the same ID/content pair is
    // idempotent, while reusing an ID for different optical truth is rejected.
    void registerPrescription(
        SequentialLensPrescription prescription,
        std::optional<LensPrescriptionAssetProvenance> provenance = std::nullopt);

    [[nodiscard]] const SequentialLensPrescription* resolve(
        std::string_view prescriptionId) const noexcept override;
    [[nodiscard]] const LensPrescriptionAssetProvenance* provenance(
        std::string_view prescriptionId) const noexcept;
    [[nodiscard]] const std::vector<SequentialLensPrescription>& entries()
        const noexcept;

private:
    std::vector<SequentialLensPrescription> prescriptions_;
    std::vector<std::optional<LensPrescriptionAssetProvenance>> provenances_;
};

// Re-expresses every prescription surface relative to its first surface and
// anchors that first vertex frame to the placed Bench component transform.
[[nodiscard]] SequentialLensPrescription placeLensPrescription(
    const SequentialLensPrescription& prescription,
    const math::RigidTransform3d& firstSurfaceFrame);

[[nodiscard]] SequentialLensPrescription makeDefaultNBk7BiconvexPrescription();

} // namespace holobench::optics::ray
