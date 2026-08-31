#include <doctest/doctest.h>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>

#include "optics/scene/OpticalBenchScene.hpp"
#include "optics/scene/SceneProjectAdapter.hpp"

namespace {

using namespace holobench;
namespace scene = holobench::optics::scene;
namespace project = holobench::project;

class AdapterTempFile final {
public:
    explicit AdapterTempFile(std::string_view stem) {
        const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path()
            / (std::string(stem) + std::to_string(unique) + ".json");
    }

    ~AdapterTempFile() {
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

} // namespace

TEST_CASE("adapter default real image scene semantic roundtrip") {
    const auto original = scene::createDefaultRealImageScene();
    const auto doc = scene::sceneToProjectDocument(original);

    CHECK(doc.formatVersion == project::kCurrentFormatVersion);
    CHECK(doc.name == original.name);
    CHECK(doc.components.size() == 4);

    const auto recovered = scene::projectDocumentToScene(doc);
    CHECK(recovered == original);
}

TEST_CASE("adapter default virtual image scene semantic roundtrip") {
    const auto original = scene::createDefaultVirtualImageScene();
    const auto doc = scene::sceneToProjectDocument(original);

    CHECK(doc.formatVersion == project::kCurrentFormatVersion);
    CHECK(doc.name == original.name);
    CHECK(doc.components.size() == 4);

    const auto recovered = scene::projectDocumentToScene(doc);
    CHECK(recovered == original);
}

TEST_CASE("adapter default infinity scene semantic roundtrip") {
    const auto original = scene::createDefaultInfinityScene();
    const auto doc = scene::sceneToProjectDocument(original);

    CHECK(doc.formatVersion == project::kCurrentFormatVersion);
    CHECK(doc.name == original.name);
    CHECK(doc.components.size() == 4);

    const auto recovered = scene::projectDocumentToScene(doc);
    CHECK(recovered == original);
}

TEST_CASE("adapter preserves exact numerical values and coordinates for custom real/virtual configurations") {
    scene::OpticalBenchScene customScene;
    customScene.name = "Custom Precision Benchmark Scene";
    customScene.source = scene::PointSource {
        .id = "src_custom",
        .positionMetres = {0.00125, -0.00375, -0.225},
        .wavelengthMetres = 632.8e-9,
        .powerWatts = 2.5,
    };
    customScene.lens = scene::ThinLensComponent {
        .id = "lens_custom",
        .planeZMetres = 0.05,
        .centreXMetres = 0.0005,
        .centreYMetres = -0.0005,
        .focalLengthMetres = -0.04, // diverging lens
        .clearApertureRadiusMetres = 0.018,
    };
    customScene.aperture = scene::CircularAperture {
        .id = "aperture_custom",
        .planeZMetres = 0.02,
        .centreXMetres = 0.0002,
        .centreYMetres = -0.0002,
        .radiusMetres = 0.012,
    };
    customScene.screen = scene::ScreenComponent {
        .id = "screen_custom",
        .planeZMetres = 0.18,
        .centreXMetres = 0.0,
        .centreYMetres = 0.0,
        .widthMetres = 0.08,
        .heightMetres = 0.05,
    };

    const auto doc = scene::sceneToProjectDocument(customScene);
    const auto recovered = scene::projectDocumentToScene(doc);

    CHECK(recovered.source.id == "src_custom");
    CHECK(recovered.source.positionMetres.x == doctest::Approx(0.00125).epsilon(1e-15));
    CHECK(recovered.source.positionMetres.y == doctest::Approx(-0.00375).epsilon(1e-15));
    CHECK(recovered.source.positionMetres.z == doctest::Approx(-0.225).epsilon(1e-15));
    CHECK(recovered.source.wavelengthMetres == doctest::Approx(632.8e-9).epsilon(1e-15));
    CHECK(recovered.source.powerWatts == doctest::Approx(2.5).epsilon(1e-15));

    CHECK(recovered.lens.id == "lens_custom");
    CHECK(recovered.lens.centreXMetres == doctest::Approx(0.0005).epsilon(1e-15));
    CHECK(recovered.lens.centreYMetres == doctest::Approx(-0.0005).epsilon(1e-15));
    CHECK(recovered.lens.planeZMetres == doctest::Approx(0.05).epsilon(1e-15));
    CHECK(recovered.lens.focalLengthMetres == doctest::Approx(-0.04).epsilon(1e-15));
    CHECK(recovered.lens.clearApertureRadiusMetres == doctest::Approx(0.018).epsilon(1e-15));

    CHECK(recovered.aperture.id == "aperture_custom");
    CHECK(recovered.aperture.centreXMetres == doctest::Approx(0.0002).epsilon(1e-15));
    CHECK(recovered.aperture.centreYMetres == doctest::Approx(-0.0002).epsilon(1e-15));
    CHECK(recovered.aperture.planeZMetres == doctest::Approx(0.02).epsilon(1e-15));
    CHECK(recovered.aperture.radiusMetres == doctest::Approx(0.012).epsilon(1e-15));

    CHECK(recovered.screen.id == "screen_custom");
    CHECK(recovered.screen.planeZMetres == doctest::Approx(0.18).epsilon(1e-15));
    CHECK(recovered.screen.widthMetres == doctest::Approx(0.08).epsilon(1e-15));
    CHECK(recovered.screen.heightMetres == doctest::Approx(0.05).epsilon(1e-15));

    CHECK(recovered == customScene);
}

TEST_CASE("adapter scene file save/load roundtrip preserves exact state and byte stability") {
    const auto original = scene::createDefaultRealImageScene();
    const AdapterTempFile firstFile("holobench-adapter-scene-1-");
    const AdapterTempFile secondFile("holobench-adapter-scene-2-");

    scene::saveScene(original, firstFile.path());
    const auto loaded = scene::loadScene(firstFile.path());
    CHECK(loaded == original);

    scene::saveScene(loaded, secondFile.path());

    std::ifstream firstInput(firstFile.path(), std::ios::binary);
    std::ifstream secondInput(secondFile.path(), std::ios::binary);
    const std::string firstBytes((std::istreambuf_iterator<char>(firstInput)), std::istreambuf_iterator<char>());
    const std::string secondBytes((std::istreambuf_iterator<char>(secondInput)), std::istreambuf_iterator<char>());
    CHECK(firstBytes == secondBytes);
}

TEST_CASE("scene project preserves lesson-template provenance") {
    const AdapterTempFile firstFile("holobench-scene-provenance-1-");
    const AdapterTempFile secondFile("holobench-scene-provenance-2-");
    const scene::OpticalBenchProject expected {
        .scene = scene::createDefaultRealImageScene(),
        .provenance = project::makeLessonTemplateProvenance(
            "lesson_thin_lens", 4),
    };
    scene::saveSceneProject(expected, firstFile.path());
    const auto restored = scene::loadSceneProject(firstFile.path());
    scene::saveSceneProject(restored, secondFile.path());
    CHECK(restored.scene == expected.scene);
    CHECK(restored.provenance == expected.provenance);
    std::ifstream firstInput(firstFile.path(), std::ios::binary);
    std::ifstream secondInput(secondFile.path(), std::ios::binary);
    const std::string firstBytes {
        std::istreambuf_iterator<char>(firstInput),
        std::istreambuf_iterator<char>()};
    const std::string secondBytes {
        std::istreambuf_iterator<char>(secondInput),
        std::istreambuf_iterator<char>()};
    CHECK(firstBytes == secondBytes);
}

TEST_CASE("legacy format one scene migrates through the normal scene loader") {
    const AdapterTempFile file("holobench-adapter-legacy-");
    {
        std::ofstream output(file.path());
        output << R"({
  "components": [
    {"id":"point_source","parameters":{"power_w":1.0,"wavelength_m":5.32e-7},"position_m":[0.0,0.0,-0.15],"type":"point_source"},
    {"id":"thin_lens","parameters":{"clear_aperture_radius_m":0.025,"focal_length_m":0.05},"position_m":[0.0,0.0,0.0],"type":"thin_lens"},
    {"id":"aperture","parameters":{"radius_m":0.025},"position_m":[0.0,0.0,0.0],"type":"circular_aperture"},
    {"id":"screen","parameters":{"height_m":0.06,"width_m":0.06},"position_m":[0.0,0.0,0.075],"type":"screen"}
  ],
  "format_version": 1,
  "name": "Legacy Optical Bench"
})";
    }
    const auto loaded = scene::loadScene(file.path());
    CHECK(loaded.name == "Legacy Optical Bench");
    CHECK(scene::isSceneValid(loaded));
}

TEST_CASE("adapter rejects document with wrong component count") {
    const auto doc = scene::sceneToProjectDocument(scene::createDefaultRealImageScene());

    // 3 components
    auto docThree = doc;
    docThree.components.pop_back();
    CHECK_THROWS_AS(static_cast<void>(scene::projectDocumentToScene(docThree)), std::invalid_argument);

    // 5 components
    auto docFive = doc;
    docFive.components.push_back(doc.components.front());
    docFive.components.back().id = "extra_source";
    CHECK_THROWS_AS(static_cast<void>(scene::projectDocumentToScene(docFive)), std::invalid_argument);
}

TEST_CASE("adapter rejects document with duplicate or unknown component types") {
    const auto doc = scene::sceneToProjectDocument(scene::createDefaultRealImageScene());

    // Duplicate thin_lens instead of screen
    auto docDuplicate = doc;
    docDuplicate.components[3].type = "thin_lens";
    docDuplicate.components[3].scalarParameters = {
        {"clear_aperture_radius_m", 0.02},
        {"focal_length_m", 0.05},
    };
    CHECK_THROWS_AS(static_cast<void>(scene::projectDocumentToScene(docDuplicate)), std::invalid_argument);

    // Unknown type
    auto docUnknown = doc;
    docUnknown.components[3].type = "beam_splitter";
    CHECK_THROWS_AS(static_cast<void>(scene::projectDocumentToScene(docUnknown)), std::invalid_argument);
}

TEST_CASE("adapter rejects missing or unknown parameters") {
    const auto doc = scene::sceneToProjectDocument(scene::createDefaultRealImageScene());

    // Missing focal_length_m in thin_lens
    auto docMissingParam = doc;
    docMissingParam.components[1].scalarParameters.erase("focal_length_m");
    CHECK_THROWS_AS(static_cast<void>(scene::projectDocumentToScene(docMissingParam)), std::invalid_argument);

    // Extra unknown parameter in point_source
    auto docExtraParam = doc;
    docExtraParam.components[0].scalarParameters["divergence_rad"] = 0.1;
    CHECK_THROWS_AS(static_cast<void>(scene::projectDocumentToScene(docExtraParam)), std::invalid_argument);
}

TEST_CASE("adapter rejects semantically invalid scenes during conversion") {
    // 1. Source placed after lens (z_source >= z_lens)
    auto docInvalidZ = scene::sceneToProjectDocument(scene::createDefaultRealImageScene());
    docInvalidZ.components[0].positionMetres[2] = 0.05;
    CHECK_THROWS_AS(static_cast<void>(scene::projectDocumentToScene(docInvalidZ)), std::invalid_argument);

    // 2. Zero focal length
    auto docZeroFocal = scene::sceneToProjectDocument(scene::createDefaultRealImageScene());
    docZeroFocal.components[1].scalarParameters["focal_length_m"] = 0.0;
    CHECK_THROWS_AS(static_cast<void>(scene::projectDocumentToScene(docZeroFocal)), std::invalid_argument);

    // 3. Negative lens clear aperture radius
    auto docNegativeRadius = scene::sceneToProjectDocument(scene::createDefaultRealImageScene());
    docNegativeRadius.components[1].scalarParameters["clear_aperture_radius_m"] = -0.01;
    CHECK_THROWS_AS(static_cast<void>(scene::projectDocumentToScene(docNegativeRadius)), std::invalid_argument);

    // 4. Negative wavelength
    auto docNegativeWavelength = scene::sceneToProjectDocument(scene::createDefaultRealImageScene());
    docNegativeWavelength.components[0].scalarParameters["wavelength_m"] = -532e-9;
    CHECK_THROWS_AS(static_cast<void>(scene::projectDocumentToScene(docNegativeWavelength)), std::invalid_argument);

    // 5. Negative power
    auto docNegativePower = scene::sceneToProjectDocument(scene::createDefaultRealImageScene());
    docNegativePower.components[0].scalarParameters["power_w"] = -1.0;
    CHECK_THROWS_AS(static_cast<void>(scene::projectDocumentToScene(docNegativePower)), std::invalid_argument);

    // 6. Non-positive screen dimensions
    auto docZeroScreen = scene::sceneToProjectDocument(scene::createDefaultRealImageScene());
    docZeroScreen.components[3].scalarParameters["width_m"] = 0.0;
    CHECK_THROWS_AS(static_cast<void>(scene::projectDocumentToScene(docZeroScreen)), std::invalid_argument);

    // 7. Duplicate component IDs across components in scene
    auto docDuplicateIds = scene::sceneToProjectDocument(scene::createDefaultRealImageScene());
    docDuplicateIds.components[1].id = docDuplicateIds.components[0].id;
    CHECK_THROWS_AS(static_cast<void>(scene::projectDocumentToScene(docDuplicateIds)), std::invalid_argument);
}

TEST_CASE("adapter rejects unsupported format version") {
    auto doc = scene::sceneToProjectDocument(scene::createDefaultRealImageScene());
    doc.formatVersion = project::kCurrentFormatVersion + 1;
    CHECK_THROWS_AS(static_cast<void>(scene::projectDocumentToScene(doc)), std::invalid_argument);

    doc.formatVersion = 0;
    CHECK_THROWS_AS(static_cast<void>(scene::projectDocumentToScene(doc)), std::invalid_argument);
}
