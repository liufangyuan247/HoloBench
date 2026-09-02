#pragma once

#include <filesystem>

#include "optics/ray/LensPrescriptionCatalog.hpp"
#include "optics/scene/BenchScene.hpp"

namespace holobench::app {

struct LoadedLensPrescriptionAsset final {
    optics::ray::SequentialLensPrescription prescription;
    optics::ray::LensPrescriptionAssetProvenance provenance;
};

[[nodiscard]] LoadedLensPrescriptionAsset loadLensPrescriptionAsset(
    const std::filesystem::path& path);

[[nodiscard]] optics::scene::CalibrationAssetReference
makeLensPrescriptionAssetReference(
    const LoadedLensPrescriptionAsset& asset,
    const optics::scene::InstrumentIdentity& identity);

// Atomically selects verified external optical truth for one placed assembly.
// Any older prescription reference is replaced and calibrated mode is enabled.
void bindLensPrescriptionAsset(
    optics::scene::BenchComponent& component,
    const LoadedLensPrescriptionAsset& asset);

// Loads every external real-lens reference relative to the project file,
// verifies exact bytes and semantic IDs, and registers immutable content.
void restoreLensPrescriptionAssets(
    const optics::scene::BenchScene& scene,
    const std::filesystem::path& projectFile,
    optics::ray::LensPrescriptionCatalog& catalog);

// Enforces that built-in prescriptions never claim external lens-prescription
// provenance and every assembly using external catalog content has one
// matching component reference. A component may still be Calibrated by other
// asset kinds. Call on every candidate edit.
void validateLensPrescriptionAssetBindings(
    const optics::scene::BenchScene& scene,
    const optics::ray::LensPrescriptionCatalog& catalog);

} // namespace holobench::app
