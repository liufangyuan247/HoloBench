#include "optics/scene/SceneProjectAdapter.hpp"

#include <stdexcept>
#include <string>

namespace holobench::optics::scene {

project::ProjectDocument sceneToProjectDocument(const OpticalBenchScene& scene) {
    validateScene(scene);

    project::ProjectDocument doc;
    doc.formatVersion = project::kCurrentFormatVersion;
    doc.name = scene.name;

    // 1. Point source
    project::ComponentRecord sourceRecord;
    sourceRecord.id = scene.source.id;
    sourceRecord.type = "point_source";
    sourceRecord.positionMetres[0] = scene.source.positionMetres.x;
    sourceRecord.positionMetres[1] = scene.source.positionMetres.y;
    sourceRecord.positionMetres[2] = scene.source.positionMetres.z;
    sourceRecord.scalarParameters = {
        {"power_w", scene.source.powerWatts},
        {"wavelength_m", scene.source.wavelengthMetres},
    };
    doc.components.push_back(std::move(sourceRecord));

    // 2. Thin lens
    project::ComponentRecord lensRecord;
    lensRecord.id = scene.lens.id;
    lensRecord.type = "thin_lens";
    lensRecord.positionMetres[0] = scene.lens.centreXMetres;
    lensRecord.positionMetres[1] = scene.lens.centreYMetres;
    lensRecord.positionMetres[2] = scene.lens.planeZMetres;
    lensRecord.scalarParameters = {
        {"clear_aperture_radius_m", scene.lens.clearApertureRadiusMetres},
        {"focal_length_m", scene.lens.focalLengthMetres},
    };
    doc.components.push_back(std::move(lensRecord));

    // 3. Circular aperture
    project::ComponentRecord apertureRecord;
    apertureRecord.id = scene.aperture.id;
    apertureRecord.type = "circular_aperture";
    apertureRecord.positionMetres[0] = scene.aperture.centreXMetres;
    apertureRecord.positionMetres[1] = scene.aperture.centreYMetres;
    apertureRecord.positionMetres[2] = scene.aperture.planeZMetres;
    apertureRecord.scalarParameters = {
        {"radius_m", scene.aperture.radiusMetres},
    };
    doc.components.push_back(std::move(apertureRecord));

    // 4. Screen
    project::ComponentRecord screenRecord;
    screenRecord.id = scene.screen.id;
    screenRecord.type = "screen";
    screenRecord.positionMetres[0] = scene.screen.centreXMetres;
    screenRecord.positionMetres[1] = scene.screen.centreYMetres;
    screenRecord.positionMetres[2] = scene.screen.planeZMetres;
    screenRecord.scalarParameters = {
        {"height_m", scene.screen.heightMetres},
        {"width_m", scene.screen.widthMetres},
    };
    doc.components.push_back(std::move(screenRecord));

    return doc;
}

OpticalBenchScene projectDocumentToScene(const project::ProjectDocument& document) {
    if (document.formatVersion != project::kCurrentFormatVersion) {
        throw std::invalid_argument(
            "unsupported project format version: " + std::to_string(document.formatVersion));
    }

    if (document.components.size() != 4) {
        throw std::invalid_argument(
            "OpticalBenchScene requires exactly 4 components, got " + std::to_string(document.components.size()));
    }

    OpticalBenchScene scene;
    scene.name = document.name;

    bool hasSource = false;
    bool hasLens = false;
    bool hasAperture = false;
    bool hasScreen = false;

    for (const auto& comp : document.components) {
        if (comp.type == "point_source") {
            if (hasSource) {
                throw std::invalid_argument("duplicate point_source component in document");
            }
            hasSource = true;
            if (comp.id.empty()) {
                throw std::invalid_argument("point_source id must not be empty");
            }
            if (comp.scalarParameters.size() != 2) {
                throw std::invalid_argument("point_source must have exactly 2 parameters (wavelength_m, power_w)");
            }
            const auto waveIt = comp.scalarParameters.find("wavelength_m");
            const auto powerIt = comp.scalarParameters.find("power_w");
            if (waveIt == comp.scalarParameters.end() || powerIt == comp.scalarParameters.end()) {
                throw std::invalid_argument("point_source missing required parameters (wavelength_m, power_w)");
            }
            scene.source.id = comp.id;
            scene.source.positionMetres = {comp.positionMetres[0], comp.positionMetres[1], comp.positionMetres[2]};
            scene.source.wavelengthMetres = waveIt->second;
            scene.source.powerWatts = powerIt->second;
        } else if (comp.type == "thin_lens") {
            if (hasLens) {
                throw std::invalid_argument("duplicate thin_lens component in document");
            }
            hasLens = true;
            if (comp.id.empty()) {
                throw std::invalid_argument("thin_lens id must not be empty");
            }
            if (comp.scalarParameters.size() != 2) {
                throw std::invalid_argument("thin_lens must have exactly 2 parameters (focal_length_m, clear_aperture_radius_m)");
            }
            const auto focalIt = comp.scalarParameters.find("focal_length_m");
            const auto clearIt = comp.scalarParameters.find("clear_aperture_radius_m");
            if (focalIt == comp.scalarParameters.end() || clearIt == comp.scalarParameters.end()) {
                throw std::invalid_argument("thin_lens missing required parameters (focal_length_m, clear_aperture_radius_m)");
            }
            scene.lens.id = comp.id;
            scene.lens.centreXMetres = comp.positionMetres[0];
            scene.lens.centreYMetres = comp.positionMetres[1];
            scene.lens.planeZMetres = comp.positionMetres[2];
            scene.lens.focalLengthMetres = focalIt->second;
            scene.lens.clearApertureRadiusMetres = clearIt->second;
        } else if (comp.type == "circular_aperture") {
            if (hasAperture) {
                throw std::invalid_argument("duplicate circular_aperture component in document");
            }
            hasAperture = true;
            if (comp.id.empty()) {
                throw std::invalid_argument("circular_aperture id must not be empty");
            }
            if (comp.scalarParameters.size() != 1) {
                throw std::invalid_argument("circular_aperture must have exactly 1 parameter (radius_m)");
            }
            const auto radIt = comp.scalarParameters.find("radius_m");
            if (radIt == comp.scalarParameters.end()) {
                throw std::invalid_argument("circular_aperture missing required parameter (radius_m)");
            }
            scene.aperture.id = comp.id;
            scene.aperture.centreXMetres = comp.positionMetres[0];
            scene.aperture.centreYMetres = comp.positionMetres[1];
            scene.aperture.planeZMetres = comp.positionMetres[2];
            scene.aperture.radiusMetres = radIt->second;
        } else if (comp.type == "screen") {
            if (hasScreen) {
                throw std::invalid_argument("duplicate screen component in document");
            }
            hasScreen = true;
            if (comp.id.empty()) {
                throw std::invalid_argument("screen id must not be empty");
            }
            if (comp.scalarParameters.size() != 2) {
                throw std::invalid_argument("screen must have exactly 2 parameters (width_m, height_m)");
            }
            const auto widthIt = comp.scalarParameters.find("width_m");
            const auto heightIt = comp.scalarParameters.find("height_m");
            if (widthIt == comp.scalarParameters.end() || heightIt == comp.scalarParameters.end()) {
                throw std::invalid_argument("screen missing required parameters (width_m, height_m)");
            }
            scene.screen.id = comp.id;
            scene.screen.centreXMetres = comp.positionMetres[0];
            scene.screen.centreYMetres = comp.positionMetres[1];
            scene.screen.planeZMetres = comp.positionMetres[2];
            scene.screen.widthMetres = widthIt->second;
            scene.screen.heightMetres = heightIt->second;
        } else {
            throw std::invalid_argument("unknown component type: " + comp.type);
        }
    }

    if (!hasSource || !hasLens || !hasAperture || !hasScreen) {
        throw std::invalid_argument("document missing required components");
    }

    validateScene(scene);
    return scene;
}

void saveScene(const OpticalBenchScene& scene, const std::filesystem::path& path) {
    const auto document = sceneToProjectDocument(scene);
    project::save(document, path);
}

OpticalBenchScene loadScene(const std::filesystem::path& path) {
    const auto document = project::load(path);
    return projectDocumentToScene(document);
}

} // namespace holobench::optics::scene
