#include "app/ChimeraPerspectiveManifest.hpp"

#include <fstream>
#include <initializer_list>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace holobench::app::chimera {
namespace {

using Json = nlohmann::json;

void requireKeys(const Json &value,
                 std::initializer_list<std::string_view> keys,
                 std::string_view context) {
  if (!value.is_object() || value.size() != keys.size()) {
    throw std::runtime_error(std::string(context) +
                             " has missing or unknown keys");
  }
  for (const auto key : keys) {
    if (!value.contains(key)) {
      throw std::runtime_error(std::string(context) +
                               " has missing or unknown keys");
    }
  }
}

RgbTransferFunction parseTransfer(std::string_view value) {
  if (value == "linear")
    return RgbTransferFunction::Linear;
  if (value == "srgb")
    return RgbTransferFunction::Srgb;
  throw std::runtime_error(
      "perspective manifest transfer_function must be linear or srgb");
}

} // namespace

std::vector<PerspectiveViewImage>
loadPerspectiveViewManifest(const std::filesystem::path &manifestPath,
                            const ChimeraRecipe &recipe,
                            const PerspectiveImageAdapterOptions &options) {
  if (manifestPath.empty()) {
    throw std::invalid_argument("perspective manifest path must not be empty");
  }
  std::error_code sizeError;
  const std::uintmax_t byteCount =
      std::filesystem::file_size(manifestPath, sizeError);
  if (sizeError || byteCount == 0U ||
      byteCount > kMaximumPerspectiveManifestBytes) {
    throw std::runtime_error(
        "perspective manifest is missing, empty, or exceeds 16 MiB");
  }
  std::ifstream input(manifestPath, std::ios::binary);
  if (!input) {
    throw std::runtime_error("unable to open perspective manifest: " +
                             manifestPath.string());
  }
  const std::string contents((std::istreambuf_iterator<char>(input)),
                             std::istreambuf_iterator<char>());
  try {
    const Json document = Json::parse(contents);
    requireKeys(document, {"angle_unit", "format", "format_version", "views"},
                "perspective manifest");
    if (document.at("format").get<std::string>() !=
            "holobench_chimera_perspective_manifest" ||
        document.at("format_version").get<int>() !=
            kPerspectiveManifestFormatVersion ||
        document.at("angle_unit").get<std::string>() != "rad") {
      throw std::runtime_error(
          "unsupported perspective manifest format, version, or units");
    }
    const Json &views = document.at("views");
    if (!views.is_array() || views.size() < 2U ||
        views.size() > kMaximumPerspectiveManifestViews) {
      throw std::runtime_error(
          "perspective manifest must contain between 2 and 256 views");
    }
    std::vector<PerspectiveRasterView> rasters;
    rasters.reserve(views.size());
    for (const auto &encoded : views) {
      requireKeys(encoded,
                  {"horizontal_angle_rad", "path", "transfer_function",
                   "vertical_angle_rad", "view_id"},
                  "perspective manifest view");
      const std::filesystem::path declared =
          encoded.at("path").get<std::string>();
      if (declared.empty()) {
        throw std::runtime_error(
            "perspective manifest view path must not be empty");
      }
      const auto transfer =
          parseTransfer(encoded.at("transfer_function").get<std::string>());
      const auto resolved = declared.is_absolute()
                                ? declared
                                : manifestPath.parent_path() / declared;
      rasters.push_back({
          .viewId = encoded.at("view_id").get<std::string>(),
          .horizontalAngleRadians =
              encoded.at("horizontal_angle_rad").get<double>(),
          .verticalAngleRadians =
              encoded.at("vertical_angle_rad").get<double>(),
          .image = loadPortablePixmap(resolved, transfer),
      });
    }
    return adaptPerspectiveRasterViews(recipe, std::move(rasters), options);
  } catch (const Json::exception &error) {
    throw std::runtime_error(
        std::string("invalid perspective manifest JSON: ") + error.what());
  } catch (const std::invalid_argument &error) {
    throw std::runtime_error(std::string("invalid perspective manifest: ") +
                             error.what());
  }
}

} // namespace holobench::app::chimera
