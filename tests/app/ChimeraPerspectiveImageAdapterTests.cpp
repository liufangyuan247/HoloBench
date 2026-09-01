#include <doctest/doctest.h>

#include <atomic>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "app/ChimeraPerspectiveImageAdapter.hpp"

namespace chimera = holobench::app::chimera;

namespace {

class TemporaryPixmap final {
public:
    explicit TemporaryPixmap(std::string extension) {
        static std::atomic<unsigned int> sequence {0U};
        path_ = std::filesystem::temp_directory_path()
            / ("holobench-chimera-perspective-"
                + std::to_string(sequence.fetch_add(1U)) + extension);
    }

    ~TemporaryPixmap() {
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
    }

    TemporaryPixmap(const TemporaryPixmap&) = delete;
    TemporaryPixmap& operator=(const TemporaryPixmap&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

private:
    std::filesystem::path path_;
};

chimera::NormalizedRgbSample sample(double red, double green, double blue) {
    return {.red = red, .green = green, .blue = blue};
}

} // namespace

TEST_CASE("real linear raster is area-resampled into the hogel grid") {
    auto recipe = chimera::makeCanonicalChimeraRecipe();
    recipe.hogels.countX = 2U;
    recipe.hogels.countY = 2U;
    const chimera::PerspectiveRasterView input {
        .viewId = "camera-centre",
        .horizontalAngleRadians = 0.0,
        .verticalAngleRadians = 0.0,
        .image = {
            .width = 4U,
            .height = 4U,
            .transferFunction = chimera::RgbTransferFunction::Linear,
            .pixels = {
                sample(1.0, 0.0, 0.0), sample(1.0, 0.0, 0.0),
                sample(0.0, 1.0, 0.0), sample(0.0, 1.0, 0.0),
                sample(1.0, 0.0, 0.0), sample(1.0, 0.0, 0.0),
                sample(0.0, 1.0, 0.0), sample(0.0, 1.0, 0.0),
                sample(0.0, 0.0, 1.0), sample(0.0, 0.0, 1.0),
                sample(0.5, 0.5, 0.5), sample(0.5, 0.5, 0.5),
                sample(0.0, 0.0, 1.0), sample(0.0, 0.0, 1.0),
                sample(0.5, 0.5, 0.5), sample(0.5, 0.5, 0.5),
            },
        },
    };

    const auto views = chimera::adaptPerspectiveRasterViews(recipe, {input});
    REQUIRE(views.size() == 1U);
    CHECK(views.front().width == 2U);
    CHECK(views.front().height == 2U);
    REQUIRE(views.front().pixels.size() == 4U);
    CHECK((views.front().pixels[0]
        == chimera::LinearRgb {1.0, 0.0, 0.0}));
    CHECK((views.front().pixels[1]
        == chimera::LinearRgb {0.0, 1.0, 0.0}));
    CHECK((views.front().pixels[2]
        == chimera::LinearRgb {0.0, 0.0, 1.0}));
    CHECK((views.front().pixels[3]
        == chimera::LinearRgb {0.5, 0.5, 0.5}));

    const auto dataset = chimera::generateHogelDataset(recipe, views);
    CHECK(dataset.sourceViews == views);
    CHECK(dataset.diagnostics.angularSampleCount == 4U);
    CHECK(dataset.diagnostics.slmCommandCount == 12U);
}

TEST_CASE("sRGB raster decoding follows the IEC transfer-function oracle") {
    auto recipe = chimera::makeCanonicalChimeraRecipe();
    recipe.hogels.countX = 1U;
    recipe.hogels.countY = 1U;
    const chimera::PerspectiveRasterView input {
        .viewId = "srgb-transfer-oracle",
        .image = {
            .width = 1U,
            .height = 1U,
            .transferFunction = chimera::RgbTransferFunction::Srgb,
            .pixels = {sample(0.04045, 0.5, 1.0)},
        },
    };

    const auto views = chimera::adaptPerspectiveRasterViews(recipe, {input});
    REQUIRE(views.size() == 1U);
    REQUIRE(views.front().pixels.size() == 1U);
    const auto pixel = views.front().pixels.front();
    CHECK(pixel.red == doctest::Approx(0.04045 / 12.92).epsilon(1e-14));
    CHECK(pixel.green
        == doctest::Approx(std::pow((0.5 + 0.055) / 1.055, 2.4))
            .epsilon(1e-14));
    CHECK(pixel.blue == doctest::Approx(1.0));
}

TEST_CASE("strict P3 and 16-bit P6 files load as decoder-neutral rasters") {
    TemporaryPixmap asciiFile(".ppm");
    {
        std::ofstream output(asciiFile.path(), std::ios::binary);
        output << "P3\n# measured camera export\n2 1\n15\n15 0 0  0 8 15\n";
    }
    const auto ascii = chimera::loadPortablePixmap(
        asciiFile.path(), chimera::RgbTransferFunction::Linear);
    CHECK(ascii.width == 2U);
    CHECK(ascii.height == 1U);
    CHECK(ascii.transferFunction == chimera::RgbTransferFunction::Linear);
    REQUIRE(ascii.pixels.size() == 2U);
    CHECK(ascii.pixels[0] == sample(1.0, 0.0, 0.0));
    CHECK(ascii.pixels[1].green == doctest::Approx(8.0 / 15.0));
    CHECK(ascii.pixels[1].blue == doctest::Approx(1.0));

    TemporaryPixmap binaryFile(".ppm");
    {
        std::ofstream output(binaryFile.path(), std::ios::binary);
        output << "P6\n1 1\n65535\n";
        const std::vector<unsigned char> raster {
            0xffU, 0xffU, 0x80U, 0x00U, 0x00U, 0x00U};
        output.write(reinterpret_cast<const char*>(raster.data()),
            static_cast<std::streamsize>(raster.size()));
    }
    const auto binary = chimera::loadPortablePixmap(binaryFile.path());
    REQUIRE(binary.pixels.size() == 1U);
    CHECK(binary.transferFunction == chimera::RgbTransferFunction::Srgb);
    CHECK(binary.pixels[0].red == doctest::Approx(1.0));
    CHECK(binary.pixels[0].green == doctest::Approx(32768.0 / 65535.0));
    CHECK(binary.pixels[0].blue == doctest::Approx(0.0));
}

TEST_CASE("perspective adapter rejects corrupt files identity geometry and bounds") {
    TemporaryPixmap corrupt(".ppm");
    {
        std::ofstream output(corrupt.path(), std::ios::binary);
        output << "P6\n1 1\n255\n";
        output.put(static_cast<char>(0xff));
    }
    CHECK_THROWS_AS(
        static_cast<void>(chimera::loadPortablePixmap(corrupt.path())),
        std::runtime_error);

    auto recipe = chimera::makeCanonicalChimeraRecipe();
    chimera::PerspectiveRasterView input {
        .viewId = "bounded-camera-view",
        .image = {
            .width = 1U,
            .height = 1U,
            .transferFunction = chimera::RgbTransferFunction::Linear,
            .pixels = {sample(0.25, 0.5, 0.75)},
        },
    };
    CHECK_THROWS_AS(
        static_cast<void>(chimera::adaptPerspectiveRasterViews(
            recipe, {input, input})),
        std::invalid_argument);

    input.horizontalAngleRadians
        = recipe.targetHorizontalFieldOfViewRadians;
    CHECK_THROWS_AS(
        static_cast<void>(chimera::adaptPerspectiveRasterViews(recipe, {input})),
        std::invalid_argument);

    input.horizontalAngleRadians = 0.0;
    input.image.pixels.clear();
    CHECK_THROWS_AS(
        static_cast<void>(chimera::adaptPerspectiveRasterViews(recipe, {input})),
        std::invalid_argument);

    input.image.pixels = {sample(0.25, 0.5, 0.75)};
    CHECK_THROWS_AS(
        static_cast<void>(chimera::adaptPerspectiveRasterViews(
            recipe, {input}, {.maximumInputPixelsPerView = 0U})),
        std::invalid_argument);
}
