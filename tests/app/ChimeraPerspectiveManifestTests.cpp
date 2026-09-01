#include <doctest/doctest.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>

#include <nlohmann/json.hpp>

#include "app/ChimeraPerspectiveManifest.hpp"

namespace chimera = holobench::app::chimera;

namespace {

class TemporaryManifestDirectory final {
public:
  TemporaryManifestDirectory() {
    static std::atomic<unsigned int> sequence{0U};
    path_ = std::filesystem::temp_directory_path() /
            ("holobench-perspective-manifest-" +
             std::to_string(sequence.fetch_add(1U)));
    std::filesystem::create_directories(path_);
  }
  ~TemporaryManifestDirectory() {
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
  }
  [[nodiscard]] const std::filesystem::path &path() const noexcept {
    return path_;
  }

private:
  std::filesystem::path path_;
};

void writePpm(const std::filesystem::path &path, std::string_view rgb) {
  std::ofstream output(path, std::ios::binary);
  output << "P3\n1 1\n255\n" << rgb << "\n";
}

nlohmann::json manifestViews() {
  return nlohmann::json::array({
      {{"horizontal_angle_rad", -0.05},
       {"path", "left.ppm"},
       {"transfer_function", "linear"},
       {"vertical_angle_rad", 0.0},
       {"view_id", "measured-left"}},
      {{"horizontal_angle_rad", 0.05},
       {"path", "right.ppm"},
       {"transfer_function", "srgb"},
       {"vertical_angle_rad", 0.0},
       {"view_id", "measured-right"}},
  });
}

} // namespace

TEST_CASE(
    "perspective manifest resolves real relative PPM views into a dataset") {
  TemporaryManifestDirectory temporary;
  writePpm(temporary.path() / "left.ppm", "255 0 0");
  writePpm(temporary.path() / "right.ppm", "0 255 0");
  const nlohmann::json manifest{
      {"angle_unit", "rad"},
      {"format", "holobench_chimera_perspective_manifest"},
      {"format_version", 1},
      {"views", manifestViews()},
  };
  const auto path = temporary.path() / "views.json";
  {
    std::ofstream output(path, std::ios::binary);
    output << manifest.dump(2) << '\n';
  }
  const auto recipe = chimera::makeCanonicalChimeraRecipe();
  const auto views = chimera::loadPerspectiveViewManifest(path, recipe);
  REQUIRE(views.size() == 2U);
  CHECK(views[0].viewId == "measured-left");
  CHECK(views[1].viewId == "measured-right");
  CHECK(views[0].pixels.size() == recipe.hogels.countX * recipe.hogels.countY);
  const auto dataset = chimera::generateHogelDataset(recipe, views);
  CHECK(dataset.sourceViews == views);
  CHECK(dataset.diagnostics.angularSampleCount ==
        2U * recipe.hogels.countX * recipe.hogels.countY);
}

TEST_CASE("perspective manifest rejects schema drift and insufficient views") {
  TemporaryManifestDirectory temporary;
  writePpm(temporary.path() / "left.ppm", "255 0 0");
  auto manifest = nlohmann::json{
      {"angle_unit", "rad"},
      {"format", "holobench_chimera_perspective_manifest"},
      {"format_version", 1},
      {"views", manifestViews()},
  };
  manifest["views"].erase(1U);
  const auto path = temporary.path() / "invalid.json";
  {
    std::ofstream output(path, std::ios::binary);
    output << manifest.dump();
  }
  CHECK_THROWS_AS(static_cast<void>(chimera::loadPerspectiveViewManifest(
                      path, chimera::makeCanonicalChimeraRecipe())),
                  std::runtime_error);

  manifest["views"] = manifestViews();
  manifest["unknown"] = true;
  {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << manifest.dump();
  }
  CHECK_THROWS_AS(static_cast<void>(chimera::loadPerspectiveViewManifest(
                      path, chimera::makeCanonicalChimeraRecipe())),
                  std::runtime_error);
}
