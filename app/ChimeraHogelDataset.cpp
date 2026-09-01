#include "app/ChimeraHogelDataset.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <initializer_list>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <utility>

#include <nlohmann/json.hpp>

#include "optics/scene/BenchScene.hpp"

namespace holobench::app::chimera {
namespace {

using Json = nlohmann::json;
constexpr std::size_t kMaximumGeneratedAngularSamples = 5'000'000U;

void requireKeys(
    const Json& object,
    std::initializer_list<std::string_view> expected,
    std::string_view context) {
    if (!object.is_object() || object.size() != expected.size()) {
        throw std::runtime_error(
            std::string(context) + " has missing or unknown keys");
    }
    for (const auto key : expected) {
        if (!object.contains(key)) {
            throw std::runtime_error(
                std::string(context) + " has missing or unknown keys");
        }
    }
}

double finiteNumber(const Json& value, std::string_view field) {
    if (!value.is_number()) {
        throw std::runtime_error(std::string(field) + " must be numeric");
    }
    const double result = value.get<double>();
    if (!std::isfinite(result)) {
        throw std::runtime_error(std::string(field) + " must be finite");
    }
    return result;
}

std::size_t nonNegativeSize(const Json& value, std::string_view field) {
    if (!value.is_number_unsigned() && !value.is_number_integer()) {
        throw std::runtime_error(std::string(field) + " must be an integer");
    }
    const auto result = value.get<std::int64_t>();
    if (result < 0) {
        throw std::runtime_error(std::string(field) + " must be non-negative");
    }
    return static_cast<std::size_t>(result);
}

std::size_t positiveSize(const Json& value, std::string_view field) {
    const std::size_t result = nonNegativeSize(value, field);
    if (result == 0U) {
        throw std::runtime_error(std::string(field) + " must be positive");
    }
    return result;
}

std::string requiredString(const Json& value, std::string_view field) {
    if (!value.is_string()) {
        throw std::runtime_error(std::string(field) + " must be a string");
    }
    return value.get<std::string>();
}

std::size_t checkedProduct(
    std::size_t first,
    std::size_t second,
    std::string_view context) {
    if (first != 0U
        && second > std::numeric_limits<std::size_t>::max() / first) {
        throw std::overflow_error(std::string(context) + " size overflows");
    }
    return first * second;
}

void validateRgb(LinearRgb value, std::string_view context) {
    const auto valid = [](double channel) {
        return std::isfinite(channel) && channel >= 0.0 && channel <= 1.0;
    };
    if (!valid(value.red) || !valid(value.green) || !valid(value.blue)) {
        throw std::invalid_argument(
            std::string(context) + " must be finite normalized linear RGB");
    }
}

double channelValue(const LinearRgb& value, std::size_t channel) {
    switch (channel) {
    case 0U: return value.red;
    case 1U: return value.green;
    case 2U: return value.blue;
    default: throw std::logic_error("unsupported RGB channel index");
    }
}

Json rgbToJson(const LinearRgb& value) {
    return {value.red, value.green, value.blue};
}

LinearRgb rgbFromJson(const Json& value, std::string_view context) {
    if (!value.is_array() || value.size() != 3U) {
        throw std::runtime_error(
            std::string(context) + " must contain exactly three channels");
    }
    return {
        .red = finiteNumber(value.at(0), context),
        .green = finiteNumber(value.at(1), context),
        .blue = finiteNumber(value.at(2), context),
    };
}

Json payloadToJson(const HogelDataset& dataset) {
    Json views = Json::array();
    for (const auto& view : dataset.sourceViews) {
        Json pixels = Json::array();
        for (const auto& pixel : view.pixels) {
            pixels.push_back(rgbToJson(pixel));
        }
        views.push_back({
            {"horizontal_angle_rad", view.horizontalAngleRadians},
            {"pixels_linear_rgb", std::move(pixels)},
            {"view_id", view.viewId},
            {"vertical_angle_rad", view.verticalAngleRadians},
            {"height", view.height},
            {"width", view.width},
        });
    }

    Json samples = Json::array();
    for (const auto& sample : dataset.angularSamples) {
        samples.push_back({
            {"hogel_x", sample.hogelX},
            {"hogel_y", sample.hogelY},
            {"horizontal_angle_rad", sample.horizontalAngleRadians},
            {"linear_rgb", rgbToJson(sample.linearRgb)},
            {"slm_pixel_column", sample.slmPixelColumn},
            {"slm_pixel_row", sample.slmPixelRow},
            {"slm_position_x_m", sample.slmPositionXMetres},
            {"slm_position_y_m", sample.slmPositionYMetres},
            {"vertical_angle_rad", sample.verticalAngleRadians},
            {"view_id", sample.viewId},
        });
    }

    Json commands = Json::array();
    for (const auto& command : dataset.slmCommands) {
        Json pixels = Json::array();
        for (const auto& pixel : command.pixels) {
            pixels.push_back({
                {"column", pixel.column},
                {"normalized_amplitude", pixel.normalizedAmplitude},
                {"row", pixel.row},
                {"view_id", pixel.viewId},
            });
        }
        commands.push_back({
            {"channel_id", command.channelId},
            {"command_id", command.commandId},
            {"hogel_x", command.hogelX},
            {"hogel_y", command.hogelY},
            {"pixels", std::move(pixels)},
            {"wavelength_m", command.wavelengthMetres},
        });
    }

    return {
        {"angular_samples", std::move(samples)},
        {"dataset_id", dataset.datasetId},
        {"diagnostics", {
            {"all_samples_inside_slm", dataset.diagnostics.allSamplesInsideSlm},
            {"angular_sample_count", dataset.diagnostics.angularSampleCount},
            {"collided_slm_pixel_count",
                dataset.diagnostics.collidedSlmPixelCount},
            {"maximum_absolute_slm_x_m",
                dataset.diagnostics.maximumAbsoluteSlmXMetres},
            {"maximum_absolute_slm_y_m",
                dataset.diagnostics.maximumAbsoluteSlmYMetres},
            {"slm_command_count", dataset.diagnostics.slmCommandCount},
        }},
        {"hogel_geometry", {
            {"count_x", dataset.hogels.countX},
            {"count_y", dataset.hogels.countY},
            {"pitch_m", dataset.hogels.pitchMetres},
        }},
        {"slm_commands", std::move(commands)},
        {"slm_pixel_height", dataset.slmPixelHeight},
        {"slm_pixel_width", dataset.slmPixelWidth},
        {"source_recipe_id", dataset.sourceRecipeId},
        {"source_recipe_version", dataset.sourceRecipeVersion},
        {"source_views", std::move(views)},
        {"units", {
            {"angle", dataset.angleUnit},
            {"colour", dataset.colourUnit},
            {"length", dataset.lengthUnit},
            {"spatial_index", dataset.spatialIndexUnit},
        }},
    };
}

std::string fnv1a64(std::string_view bytes) {
    std::uint64_t value = 14695981039346656037ULL;
    for (const char character : bytes) {
        const auto byte = static_cast<unsigned char>(character);
        value ^= static_cast<std::uint64_t>(byte);
        value *= 1099511628211ULL;
    }
    std::ostringstream encoded;
    encoded << std::hex << std::setfill('0') << std::setw(16) << value;
    return encoded.str();
}

bool isLowerHexHash(std::string_view value) {
    return value.size() == 16U
        && std::all_of(value.begin(), value.end(), [](char character) {
            return (character >= '0' && character <= '9')
                || (character >= 'a' && character <= 'f');
        });
}

} // namespace

std::vector<PerspectiveViewImage> makeCanonicalPerspectiveViews(
    const ChimeraRecipe& recipe,
    std::size_t horizontalViewCount,
    std::size_t verticalViewCount) {
    validateChimeraRecipe(recipe);
    if (horizontalViewCount < 2U || verticalViewCount < 2U
        || horizontalViewCount > 64U || verticalViewCount > 64U) {
        throw std::invalid_argument(
            "canonical perspective view counts must be in [2, 64]");
    }
    const std::size_t hogelCount = checkedProduct(
        recipe.hogels.countX, recipe.hogels.countY, "canonical view image");
    const std::size_t viewCount = checkedProduct(
        horizontalViewCount, verticalViewCount, "canonical view grid");
    if (checkedProduct(hogelCount, viewCount, "canonical angular dataset")
        > kMaximumGeneratedAngularSamples) {
        throw std::invalid_argument(
            "canonical angular dataset exceeds the bounded sample limit");
    }

    std::vector<PerspectiveViewImage> result;
    result.reserve(viewCount);
    for (std::size_t viewY = 0; viewY < verticalViewCount; ++viewY) {
        const double viewV = static_cast<double>(viewY)
            / static_cast<double>(verticalViewCount - 1U);
        const double verticalAngle = (viewV - 0.5)
            * recipe.targetVerticalFieldOfViewRadians;
        for (std::size_t viewX = 0; viewX < horizontalViewCount; ++viewX) {
            const double viewU = static_cast<double>(viewX)
                / static_cast<double>(horizontalViewCount - 1U);
            const double horizontalAngle = (viewU - 0.5)
                * recipe.targetHorizontalFieldOfViewRadians;
            PerspectiveViewImage view {
                .viewId = "view-x" + std::to_string(viewX)
                    + "-y" + std::to_string(viewY),
                .horizontalAngleRadians = horizontalAngle,
                .verticalAngleRadians = verticalAngle,
                .width = recipe.hogels.countX,
                .height = recipe.hogels.countY,
                .pixels = {},
            };
            view.pixels.reserve(hogelCount);
            for (std::size_t y = 0; y < view.height; ++y) {
                const double v = (static_cast<double>(y) + 0.5)
                    / static_cast<double>(view.height);
                for (std::size_t x = 0; x < view.width; ++x) {
                    const double u = (static_cast<double>(x) + 0.5)
                        / static_cast<double>(view.width);
                    view.pixels.push_back({
                        .red = std::clamp(0.10 + 0.55 * u + 0.25 * viewU,
                            0.0, 1.0),
                        .green = std::clamp(0.10 + 0.55 * v + 0.25 * viewV,
                            0.0, 1.0),
                        .blue = std::clamp(
                            0.15 + 0.30 * (1.0 - u) + 0.30 * (1.0 - v)
                                + 0.20 * (1.0 - std::abs(2.0 * viewU - 1.0)),
                            0.0,
                            1.0),
                    });
                }
            }
            result.push_back(std::move(view));
        }
    }
    return result;
}

HogelDataset generateHogelDataset(
    const ChimeraRecipe& recipe,
    std::vector<PerspectiveViewImage> sourceViews) {
    validateChimeraRecipe(recipe);
    const auto compilation = compileChimeraRecipe(recipe);
    if (!compilation.feasible()) {
        throw std::invalid_argument(
            "cannot generate a hogel dataset from an unsupported recipe");
    }
    if (sourceViews.empty()) {
        throw std::invalid_argument("hogel dataset requires perspective views");
    }
    std::sort(sourceViews.begin(), sourceViews.end(), [](const auto& first, const auto& second) {
        return std::tie(
                   first.verticalAngleRadians,
                   first.horizontalAngleRadians,
                   first.viewId)
            < std::tie(
                   second.verticalAngleRadians,
                   second.horizontalAngleRadians,
                   second.viewId);
    });

    const std::size_t hogelCount = checkedProduct(
        recipe.hogels.countX, recipe.hogels.countY, "hogel grid");
    const std::size_t sampleCount = checkedProduct(
        hogelCount, sourceViews.size(), "hogel angular samples");
    if (sampleCount > kMaximumGeneratedAngularSamples) {
        throw std::invalid_argument(
            "hogel dataset exceeds the bounded angular sample limit");
    }

    std::set<std::string> viewIds;
    for (const auto& view : sourceViews) {
        if (!optics::scene::isStableBenchId(view.viewId)
            || !viewIds.insert(view.viewId).second) {
            throw std::invalid_argument(
                "perspective view IDs must be unique stable IDs");
        }
        if (view.width != recipe.hogels.countX
            || view.height != recipe.hogels.countY
            || view.pixels.size() != hogelCount) {
            throw std::invalid_argument(
                "perspective image dimensions must match the hogel grid");
        }
        if (!std::isfinite(view.horizontalAngleRadians)
            || !std::isfinite(view.verticalAngleRadians)
            || std::abs(view.horizontalAngleRadians)
                > 0.5 * recipe.targetHorizontalFieldOfViewRadians + 1e-15
            || std::abs(view.verticalAngleRadians)
                > 0.5 * recipe.targetVerticalFieldOfViewRadians + 1e-15) {
            throw std::invalid_argument(
                "perspective view angle lies outside the requested FOV");
        }
        for (const auto pixel : view.pixels) {
            validateRgb(pixel, "perspective image pixel");
        }
    }

    HogelDataset result;
    result.formatVersion = kHogelDatasetFormatVersion;
    result.datasetId = "hogel-" + recipe.recipeId;
    result.sourceRecipeId = recipe.recipeId;
    result.sourceRecipeVersion = recipe.formatVersion;
    result.hogels = recipe.hogels;
    result.slmPixelWidth = recipe.slm.pixelWidth;
    result.slmPixelHeight = recipe.slm.pixelHeight;
    result.sourceViews = std::move(sourceViews);
    result.angularSamples.reserve(sampleCount);
    result.slmCommands.reserve(checkedProduct(hogelCount, 3U, "SLM commands"));

    for (std::size_t y = 0; y < recipe.hogels.countY; ++y) {
        for (std::size_t x = 0; x < recipe.hogels.countX; ++x) {
            const std::size_t hogelIndex = y * recipe.hogels.countX + x;
            for (const auto& view : result.sourceViews) {
                const double slmX = -recipe.relay.focalLengthMetres
                    * std::tan(view.horizontalAngleRadians);
                const double slmY = -recipe.relay.focalLengthMetres
                    * std::tan(view.verticalAngleRadians);
                const bool inside = std::abs(slmX)
                        <= 0.5 * recipe.slm.widthMetres
                    && std::abs(slmY) <= 0.5 * recipe.slm.heightMetres;
                if (!inside) {
                    throw std::invalid_argument(
                        "Fourier-lens angular sample lies outside the SLM");
                }
                const double columnCoordinate = (slmX / recipe.slm.widthMetres + 0.5)
                    * static_cast<double>(recipe.slm.pixelWidth);
                const double rowCoordinate = (slmY / recipe.slm.heightMetres + 0.5)
                    * static_cast<double>(recipe.slm.pixelHeight);
                const std::size_t column = std::min(
                    static_cast<std::size_t>(std::floor(columnCoordinate)),
                    recipe.slm.pixelWidth - 1U);
                const std::size_t row = std::min(
                    static_cast<std::size_t>(std::floor(rowCoordinate)),
                    recipe.slm.pixelHeight - 1U);
                result.angularSamples.push_back({
                    .hogelX = x,
                    .hogelY = y,
                    .viewId = view.viewId,
                    .horizontalAngleRadians = view.horizontalAngleRadians,
                    .verticalAngleRadians = view.verticalAngleRadians,
                    .slmPositionXMetres = slmX,
                    .slmPositionYMetres = slmY,
                    .slmPixelColumn = column,
                    .slmPixelRow = row,
                    .linearRgb = view.pixels[hogelIndex],
                });
            }

            for (std::size_t channel = 0; channel < recipe.rgb.size(); ++channel) {
                const auto& arm = recipe.rgb[channel];
                SlmHogelCommand command {
                    .commandId = result.datasetId + "-h" + std::to_string(x)
                        + "-" + std::to_string(y) + "-" + arm.channelId,
                    .hogelX = x,
                    .hogelY = y,
                    .channelId = arm.channelId,
                    .wavelengthMetres = arm.wavelengthMetres,
                    .pixels = {},
                };
                command.pixels.reserve(result.sourceViews.size());
                std::set<std::pair<std::size_t, std::size_t>> occupied;
                const std::size_t sampleStart = hogelIndex * result.sourceViews.size();
                for (std::size_t viewIndex = 0;
                     viewIndex < result.sourceViews.size();
                     ++viewIndex) {
                    const auto& sample
                        = result.angularSamples[sampleStart + viewIndex];
                    if (!occupied.insert({
                            sample.slmPixelColumn, sample.slmPixelRow}).second) {
                        ++result.diagnostics.collidedSlmPixelCount;
                    }
                    command.pixels.push_back({
                        .viewId = sample.viewId,
                        .column = sample.slmPixelColumn,
                        .row = sample.slmPixelRow,
                        .normalizedAmplitude = std::sqrt(channelValue(
                            sample.linearRgb, channel)),
                    });
                }
                result.slmCommands.push_back(std::move(command));
            }
        }
    }

    for (const auto& sample : result.angularSamples) {
        result.diagnostics.maximumAbsoluteSlmXMetres = std::max(
            result.diagnostics.maximumAbsoluteSlmXMetres,
            std::abs(sample.slmPositionXMetres));
        result.diagnostics.maximumAbsoluteSlmYMetres = std::max(
            result.diagnostics.maximumAbsoluteSlmYMetres,
            std::abs(sample.slmPositionYMetres));
    }
    result.diagnostics.angularSampleCount = result.angularSamples.size();
    result.diagnostics.slmCommandCount = result.slmCommands.size();
    result.diagnostics.allSamplesInsideSlm = true;
    result.contentHash = computeHogelDatasetContentHash(result);
    validateHogelDataset(result);
    return result;
}

void validateHogelDataset(const HogelDataset& dataset) {
    if (dataset.formatVersion != kHogelDatasetFormatVersion) {
        throw std::invalid_argument("unsupported hogel dataset format version");
    }
    if (!optics::scene::isStableBenchId(dataset.datasetId)
        || !optics::scene::isStableBenchId(dataset.sourceRecipeId)
        || dataset.sourceRecipeVersion != kChimeraRecipeFormatVersion) {
        throw std::invalid_argument("hogel dataset source identity is invalid");
    }
    if (dataset.angleUnit != "rad" || dataset.lengthUnit != "m"
        || dataset.colourUnit != "normalized_linear_rgb"
        || dataset.spatialIndexUnit != "zero_based_hogel_and_pixel_index") {
        throw std::invalid_argument("hogel dataset units are unsupported");
    }
    if (!std::isfinite(dataset.hogels.pitchMetres)
        || dataset.hogels.pitchMetres <= 0.0
        || dataset.hogels.countX == 0U || dataset.hogels.countY == 0U
        || dataset.slmPixelWidth == 0U || dataset.slmPixelHeight == 0U
        || dataset.sourceViews.empty()) {
        throw std::invalid_argument("hogel dataset grid metadata is invalid");
    }
    const std::size_t hogelCount = checkedProduct(
        dataset.hogels.countX, dataset.hogels.countY, "hogel dataset grid");
    const std::size_t expectedSamples = checkedProduct(
        hogelCount, dataset.sourceViews.size(), "hogel dataset samples");
    const std::size_t expectedCommands = checkedProduct(
        hogelCount, 3U, "hogel dataset commands");
    if (dataset.angularSamples.size() != expectedSamples
        || dataset.slmCommands.size() != expectedCommands
        || dataset.diagnostics.angularSampleCount != expectedSamples
        || dataset.diagnostics.slmCommandCount != expectedCommands
        || !dataset.diagnostics.allSamplesInsideSlm
        || !std::isfinite(dataset.diagnostics.maximumAbsoluteSlmXMetres)
        || !std::isfinite(dataset.diagnostics.maximumAbsoluteSlmYMetres)
        || dataset.diagnostics.maximumAbsoluteSlmXMetres < 0.0
        || dataset.diagnostics.maximumAbsoluteSlmYMetres < 0.0) {
        throw std::invalid_argument("hogel dataset diagnostics or counts are invalid");
    }

    std::set<std::string> viewIds;
    const auto viewLess = [](const auto& first, const auto& second) {
        return std::tie(
                   first.verticalAngleRadians,
                   first.horizontalAngleRadians,
                   first.viewId)
            < std::tie(
                   second.verticalAngleRadians,
                   second.horizontalAngleRadians,
                   second.viewId);
    };
    if (!std::is_sorted(
            dataset.sourceViews.begin(), dataset.sourceViews.end(), viewLess)) {
        throw std::invalid_argument(
            "hogel dataset source views are not in canonical order");
    }
    for (const auto& view : dataset.sourceViews) {
        if (!optics::scene::isStableBenchId(view.viewId)
            || !viewIds.insert(view.viewId).second
            || view.width != dataset.hogels.countX
            || view.height != dataset.hogels.countY
            || view.pixels.size() != hogelCount
            || !std::isfinite(view.horizontalAngleRadians)
            || !std::isfinite(view.verticalAngleRadians)) {
            throw std::invalid_argument("hogel dataset source view is invalid");
        }
        for (const auto pixel : view.pixels) {
            validateRgb(pixel, "hogel dataset source pixel");
        }
    }
    for (std::size_t sampleIndex = 0;
         sampleIndex < dataset.angularSamples.size();
         ++sampleIndex) {
        const auto& sample = dataset.angularSamples[sampleIndex];
        const std::size_t hogelIndex = sampleIndex / dataset.sourceViews.size();
        const std::size_t viewIndex = sampleIndex % dataset.sourceViews.size();
        const std::size_t expectedX = hogelIndex % dataset.hogels.countX;
        const std::size_t expectedY = hogelIndex / dataset.hogels.countX;
        const auto& expectedView = dataset.sourceViews[viewIndex];
        if (sample.hogelX >= dataset.hogels.countX
            || sample.hogelY >= dataset.hogels.countY
            || viewIds.find(sample.viewId) == viewIds.end()
            || sample.slmPixelColumn >= dataset.slmPixelWidth
            || sample.slmPixelRow >= dataset.slmPixelHeight
            || !std::isfinite(sample.horizontalAngleRadians)
            || !std::isfinite(sample.verticalAngleRadians)
            || !std::isfinite(sample.slmPositionXMetres)
            || !std::isfinite(sample.slmPositionYMetres)) {
            throw std::invalid_argument("hogel angular sample is invalid");
        }
        validateRgb(sample.linearRgb, "hogel angular sample colour");
        if (sample.hogelX != expectedX || sample.hogelY != expectedY
            || sample.viewId != expectedView.viewId
            || sample.horizontalAngleRadians
                != expectedView.horizontalAngleRadians
            || sample.verticalAngleRadians != expectedView.verticalAngleRadians
            || sample.linearRgb != expectedView.pixels[hogelIndex]) {
            throw std::invalid_argument(
                "hogel angular samples are not canonical source mappings");
        }
    }

    const std::array<std::string_view, 3> channelIds {"red", "green", "blue"};
    std::set<std::string> commandIds;
    std::size_t collidedPixelCount = 0U;
    for (std::size_t commandIndex = 0;
         commandIndex < dataset.slmCommands.size();
         ++commandIndex) {
        const auto& command = dataset.slmCommands[commandIndex];
        const std::size_t hogelIndex = commandIndex / channelIds.size();
        const std::size_t channelIndex = commandIndex % channelIds.size();
        const std::size_t expectedX = hogelIndex % dataset.hogels.countX;
        const std::size_t expectedY = hogelIndex / dataset.hogels.countX;
        if (!optics::scene::isStableBenchId(command.commandId)
            || !commandIds.insert(command.commandId).second
            || command.hogelX >= dataset.hogels.countX
            || command.hogelY >= dataset.hogels.countY
            || std::find(channelIds.begin(), channelIds.end(), command.channelId)
                == channelIds.end()
            || !std::isfinite(command.wavelengthMetres)
            || command.wavelengthMetres <= 0.0
            || command.pixels.size() != dataset.sourceViews.size()) {
            throw std::invalid_argument("hogel SLM command is invalid");
        }
        if (command.hogelX != expectedX || command.hogelY != expectedY
            || command.channelId != channelIds[channelIndex]) {
            throw std::invalid_argument(
                "hogel SLM commands are not in canonical order");
        }
        std::set<std::pair<std::size_t, std::size_t>> occupied;
        for (std::size_t viewIndex = 0;
             viewIndex < command.pixels.size();
             ++viewIndex) {
            const auto& pixel = command.pixels[viewIndex];
            if (viewIds.find(pixel.viewId) == viewIds.end()
                || pixel.column >= dataset.slmPixelWidth
                || pixel.row >= dataset.slmPixelHeight
                || !std::isfinite(pixel.normalizedAmplitude)
                || pixel.normalizedAmplitude < 0.0
                || pixel.normalizedAmplitude > 1.0) {
                throw std::invalid_argument("hogel SLM command pixel is invalid");
            }
            const auto& sample = dataset.angularSamples[
                hogelIndex * dataset.sourceViews.size() + viewIndex];
            if (pixel.viewId != sample.viewId
                || pixel.column != sample.slmPixelColumn
                || pixel.row != sample.slmPixelRow
                || pixel.normalizedAmplitude
                    != std::sqrt(channelValue(
                        sample.linearRgb, channelIndex))) {
                throw std::invalid_argument(
                    "hogel SLM command pixel does not match its angular sample");
            }
            if (!occupied.insert({pixel.column, pixel.row}).second) {
                ++collidedPixelCount;
            }
        }
    }
    double maximumAbsoluteX = 0.0;
    double maximumAbsoluteY = 0.0;
    for (const auto& sample : dataset.angularSamples) {
        maximumAbsoluteX
            = std::max(maximumAbsoluteX, std::abs(sample.slmPositionXMetres));
        maximumAbsoluteY
            = std::max(maximumAbsoluteY, std::abs(sample.slmPositionYMetres));
    }
    if (dataset.diagnostics.collidedSlmPixelCount != collidedPixelCount
        || dataset.diagnostics.maximumAbsoluteSlmXMetres != maximumAbsoluteX
        || dataset.diagnostics.maximumAbsoluteSlmYMetres != maximumAbsoluteY) {
        throw std::invalid_argument(
            "hogel dataset diagnostics do not match their samples");
    }
    if (dataset.hashAlgorithm != kHogelDatasetHashAlgorithm
        || !isLowerHexHash(dataset.contentHash)) {
        throw std::invalid_argument("hogel dataset content hash metadata is invalid");
    }
}

std::string computeHogelDatasetContentHash(const HogelDataset& dataset) {
    return fnv1a64(payloadToJson(dataset).dump());
}

std::string serializeHogelDataset(const HogelDataset& dataset) {
    validateHogelDataset(dataset);
    const std::string computed = computeHogelDatasetContentHash(dataset);
    if (computed != dataset.contentHash) {
        throw std::invalid_argument(
            "hogel dataset content hash does not match its payload");
    }
    const Json root {
        {"content_hash", dataset.contentHash},
        {"document_type", "chimera_hogel_dataset"},
        {"format_version", dataset.formatVersion},
        {"hash_algorithm", dataset.hashAlgorithm},
        {"payload", payloadToJson(dataset)},
    };
    return root.dump(2) + "\n";
}

HogelDataset parseHogelDataset(std::string_view jsonText) {
    try {
        const Json root = Json::parse(jsonText);
        requireKeys(root,
            {"content_hash", "document_type", "format_version",
             "hash_algorithm", "payload"},
            "hogel dataset");
        if (requiredString(root.at("document_type"), "document_type")
            != "chimera_hogel_dataset") {
            throw std::runtime_error(
                "project is not a chimera_hogel_dataset document");
        }
        if (!root.at("format_version").is_number_integer()) {
            throw std::runtime_error("format_version must be an integer");
        }
        const auto& payload = root.at("payload");
        requireKeys(payload,
            {"angular_samples", "dataset_id", "diagnostics", "hogel_geometry",
             "slm_commands", "slm_pixel_height", "slm_pixel_width",
             "source_recipe_id", "source_recipe_version", "source_views",
             "units"},
            "hogel dataset payload");
        HogelDataset result;
        result.formatVersion = root.at("format_version").get<int>();
        result.hashAlgorithm = requiredString(
            root.at("hash_algorithm"), "hash_algorithm");
        result.contentHash = requiredString(
            root.at("content_hash"), "content_hash");
        result.datasetId = requiredString(payload.at("dataset_id"), "dataset_id");
        result.sourceRecipeId = requiredString(
            payload.at("source_recipe_id"), "source_recipe_id");
        if (!payload.at("source_recipe_version").is_number_integer()) {
            throw std::runtime_error("source_recipe_version must be an integer");
        }
        result.sourceRecipeVersion
            = payload.at("source_recipe_version").get<int>();

        const auto& units = payload.at("units");
        requireKeys(units, {"angle", "colour", "length", "spatial_index"},
            "hogel dataset units");
        result.angleUnit = requiredString(units.at("angle"), "angle unit");
        result.lengthUnit = requiredString(units.at("length"), "length unit");
        result.colourUnit = requiredString(units.at("colour"), "colour unit");
        result.spatialIndexUnit = requiredString(
            units.at("spatial_index"), "spatial index unit");

        const auto& hogels = payload.at("hogel_geometry");
        requireKeys(hogels, {"count_x", "count_y", "pitch_m"},
            "hogel dataset geometry");
        result.hogels = {
            .pitchMetres = finiteNumber(hogels.at("pitch_m"), "hogel pitch_m"),
            .countX = positiveSize(hogels.at("count_x"), "hogel count_x"),
            .countY = positiveSize(hogels.at("count_y"), "hogel count_y"),
        };
        result.slmPixelWidth = positiveSize(
            payload.at("slm_pixel_width"), "SLM pixel width");
        result.slmPixelHeight = positiveSize(
            payload.at("slm_pixel_height"), "SLM pixel height");

        const auto& views = payload.at("source_views");
        if (!views.is_array()) {
            throw std::runtime_error("source_views must be an array");
        }
        const std::size_t hogelCount = checkedProduct(
            result.hogels.countX, result.hogels.countY,
            "parsed hogel dataset grid");
        if (views.empty()
            || checkedProduct(hogelCount, views.size(),
                   "parsed hogel angular samples")
                > kMaximumGeneratedAngularSamples) {
            throw std::runtime_error(
                "source_views exceed the bounded dataset limit");
        }
        result.sourceViews.reserve(views.size());
        for (const auto& encoded : views) {
            requireKeys(encoded,
                {"height", "horizontal_angle_rad", "pixels_linear_rgb",
                 "vertical_angle_rad", "view_id", "width"},
                "perspective view");
            PerspectiveViewImage view {
                .viewId = requiredString(encoded.at("view_id"), "view_id"),
                .horizontalAngleRadians = finiteNumber(
                    encoded.at("horizontal_angle_rad"), "horizontal_angle_rad"),
                .verticalAngleRadians = finiteNumber(
                    encoded.at("vertical_angle_rad"), "vertical_angle_rad"),
                .width = positiveSize(encoded.at("width"), "view width"),
                .height = positiveSize(encoded.at("height"), "view height"),
                .pixels = {},
            };
            const auto& pixels = encoded.at("pixels_linear_rgb");
            if (!pixels.is_array() || pixels.size() != hogelCount) {
                throw std::runtime_error(
                    "view pixels must match the hogel grid");
            }
            view.pixels.reserve(pixels.size());
            for (const auto& pixel : pixels) {
                view.pixels.push_back(rgbFromJson(pixel, "view pixel"));
            }
            result.sourceViews.push_back(std::move(view));
        }

        const auto& samples = payload.at("angular_samples");
        const std::size_t expectedSampleCount = checkedProduct(
            hogelCount, result.sourceViews.size(),
            "parsed hogel angular samples");
        if (!samples.is_array() || samples.size() != expectedSampleCount) {
            throw std::runtime_error(
                "angular_samples must match the hogel and view grids");
        }
        result.angularSamples.reserve(samples.size());
        for (const auto& encoded : samples) {
            requireKeys(encoded,
                {"hogel_x", "hogel_y", "horizontal_angle_rad", "linear_rgb",
                 "slm_pixel_column", "slm_pixel_row", "slm_position_x_m",
                 "slm_position_y_m", "vertical_angle_rad", "view_id"},
                "hogel angular sample");
            result.angularSamples.push_back({
                .hogelX = nonNegativeSize(encoded.at("hogel_x"), "hogel_x"),
                .hogelY = nonNegativeSize(encoded.at("hogel_y"), "hogel_y"),
                .viewId = requiredString(encoded.at("view_id"), "view_id"),
                .horizontalAngleRadians = finiteNumber(
                    encoded.at("horizontal_angle_rad"), "horizontal_angle_rad"),
                .verticalAngleRadians = finiteNumber(
                    encoded.at("vertical_angle_rad"), "vertical_angle_rad"),
                .slmPositionXMetres = finiteNumber(
                    encoded.at("slm_position_x_m"), "slm_position_x_m"),
                .slmPositionYMetres = finiteNumber(
                    encoded.at("slm_position_y_m"), "slm_position_y_m"),
                .slmPixelColumn = nonNegativeSize(
                    encoded.at("slm_pixel_column"), "slm_pixel_column"),
                .slmPixelRow = nonNegativeSize(
                    encoded.at("slm_pixel_row"), "slm_pixel_row"),
                .linearRgb = rgbFromJson(encoded.at("linear_rgb"), "linear_rgb"),
            });
        }

        const auto& commands = payload.at("slm_commands");
        const std::size_t expectedCommandCount = checkedProduct(
            hogelCount, 3U, "parsed hogel SLM commands");
        if (!commands.is_array() || commands.size() != expectedCommandCount) {
            throw std::runtime_error(
                "slm_commands must contain three commands per hogel");
        }
        result.slmCommands.reserve(commands.size());
        for (const auto& encoded : commands) {
            requireKeys(encoded,
                {"channel_id", "command_id", "hogel_x", "hogel_y", "pixels",
                 "wavelength_m"},
                "SLM hogel command");
            SlmHogelCommand command {
                .commandId = requiredString(encoded.at("command_id"), "command_id"),
                .hogelX = nonNegativeSize(encoded.at("hogel_x"), "hogel_x"),
                .hogelY = nonNegativeSize(encoded.at("hogel_y"), "hogel_y"),
                .channelId = requiredString(encoded.at("channel_id"), "channel_id"),
                .wavelengthMetres = finiteNumber(
                    encoded.at("wavelength_m"), "wavelength_m"),
                .pixels = {},
            };
            const auto& pixels = encoded.at("pixels");
            if (!pixels.is_array()
                || pixels.size() != result.sourceViews.size()) {
                throw std::runtime_error(
                    "SLM command pixels must match the source views");
            }
            command.pixels.reserve(pixels.size());
            for (const auto& pixel : pixels) {
                requireKeys(pixel,
                    {"column", "normalized_amplitude", "row", "view_id"},
                    "SLM command pixel");
                command.pixels.push_back({
                    .viewId = requiredString(pixel.at("view_id"), "view_id"),
                    .column = nonNegativeSize(pixel.at("column"), "pixel column"),
                    .row = nonNegativeSize(pixel.at("row"), "pixel row"),
                    .normalizedAmplitude = finiteNumber(
                        pixel.at("normalized_amplitude"),
                        "normalized_amplitude"),
                });
            }
            result.slmCommands.push_back(std::move(command));
        }

        const auto& diagnostics = payload.at("diagnostics");
        requireKeys(diagnostics,
            {"all_samples_inside_slm", "angular_sample_count",
             "collided_slm_pixel_count", "maximum_absolute_slm_x_m",
             "maximum_absolute_slm_y_m", "slm_command_count"},
            "hogel dataset diagnostics");
        if (!diagnostics.at("all_samples_inside_slm").is_boolean()) {
            throw std::runtime_error("all_samples_inside_slm must be boolean");
        }
        result.diagnostics = {
            .angularSampleCount = nonNegativeSize(
                diagnostics.at("angular_sample_count"), "angular_sample_count"),
            .slmCommandCount = nonNegativeSize(
                diagnostics.at("slm_command_count"), "slm_command_count"),
            .collidedSlmPixelCount = nonNegativeSize(
                diagnostics.at("collided_slm_pixel_count"),
                "collided_slm_pixel_count"),
            .maximumAbsoluteSlmXMetres = finiteNumber(
                diagnostics.at("maximum_absolute_slm_x_m"),
                "maximum_absolute_slm_x_m"),
            .maximumAbsoluteSlmYMetres = finiteNumber(
                diagnostics.at("maximum_absolute_slm_y_m"),
                "maximum_absolute_slm_y_m"),
            .allSamplesInsideSlm
                = diagnostics.at("all_samples_inside_slm").get<bool>(),
        };

        validateHogelDataset(result);
        const std::string computed = computeHogelDatasetContentHash(result);
        if (computed != result.contentHash) {
            throw std::runtime_error(
                "hogel dataset content hash does not match its payload");
        }
        return result;
    } catch (const nlohmann::json::exception& error) {
        throw std::runtime_error(
            std::string("invalid hogel dataset JSON: ") + error.what());
    } catch (const std::invalid_argument& error) {
        throw std::runtime_error(
            std::string("invalid hogel dataset: ") + error.what());
    }
}

} // namespace holobench::app::chimera
