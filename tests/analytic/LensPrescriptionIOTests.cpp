#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>

#include "optics/io/LensPrescriptionIO.hpp"

namespace io = holobench::optics::io;
namespace material = holobench::optics::material;
namespace ray = holobench::optics::ray;

namespace {

[[nodiscard]] ray::SequentialLensPrescription makePrescription() {
  auto cauchy = material::OpticalMaterial{
      .id = "teaching_cauchy",
      .displayName = "Cauchy, \"teaching\"\nmaterial",
      .wavelengthDomain = {.minimumMetres = 400e-9, .maximumMetres = 700e-9},
      .dispersion =
          material::CauchyModelSi{
              .aDimensionless = 1.5,
              .bSquareMetres = 1e-15,
              .cFourthMetres = 2e-29,
          },
  };
  return {
      .id = "roundtrip, \"lens\"",
      .materials =
          {
              material::makeVacuumMaterial(),
              std::move(cauchy),
              material::makeSchottNBk7Material(),
          },
      .surfaces =
          {
              {
                  .id = "front,asphere",
                  .geometry =
                      {
                          .curvaturePerMetre = 5.0,
                          .conicConstant = -0.5,
                          .evenAsphereTerms =
                              {
                                  {.radialOrder = 4, .coefficientSi = 1000.0},
                                  {.radialOrder = 6, .coefficientSi = -2.5e6},
                              },
                          .clearSemiDiameterMetres = 0.01,
                      },
                  .localToWorld = {.translationMetres = {0.001, -0.002, 0.01}},
                  .materialBeforeId = "vacuum",
                  .materialAfterId = "teaching_cauchy",
              },
              {
                  .id = "rear",
                  .geometry =
                      {
                          .curvaturePerMetre = -4.0,
                          .conicConstant = 0.0,
                          .evenAsphereTerms = {},
                          .clearSemiDiameterMetres = 0.009,
                      },
                  .localToWorld =
                      {
                          .translationMetres = {0.0, 0.0, 0.02},
                          .localXAxisInWorld = {0.0, 1.0, 0.0},
                          .localYAxisInWorld = {-1.0, 0.0, 0.0},
                          .localZAxisInWorld = {0.0, 0.0, 1.0},
                      },
                  .materialBeforeId = "teaching_cauchy",
                  .materialAfterId = "schott_n_bk7",
              },
          },
  };
}

[[nodiscard]] std::string replacedOnce(std::string source,
                                       std::string_view needle,
                                       std::string_view replacement) {
  const std::size_t position = source.find(needle);
  if (position == std::string::npos) {
    throw std::logic_error("test mutation needle is absent");
  }
  source.replace(position, needle.size(), replacement);
  return source;
}

class TemporaryPrescriptionFiles final {
public:
  TemporaryPrescriptionFiles() {
    const auto unique =
        std::chrono::steady_clock::now().time_since_epoch().count();
    const auto base = std::filesystem::temp_directory_path() /
                      ("holobench-prescription-" + std::to_string(unique));
    jsonPath_ = base;
    jsonPath_ += ".json";
    csvPath_ = base;
    csvPath_ += ".csv";
  }

  ~TemporaryPrescriptionFiles() {
    std::error_code ignored;
    std::filesystem::remove(jsonPath_, ignored);
    std::filesystem::remove(csvPath_, ignored);
  }

  [[nodiscard]] const std::filesystem::path &jsonPath() const noexcept {
    return jsonPath_;
  }
  [[nodiscard]] const std::filesystem::path &csvPath() const noexcept {
    return csvPath_;
  }

private:
  std::filesystem::path jsonPath_;
  std::filesystem::path csvPath_;
};

[[nodiscard]] std::string readBytes(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>());
}

} // namespace

TEST_CASE(
    "versioned lens prescription JSON round trip is lossless and byte stable") {
  const auto expected = makePrescription();
  const std::string first = io::serializeLensPrescriptionJson(expected);
  const auto actual = io::parseLensPrescriptionJson(first);
  const std::string second = io::serializeLensPrescriptionJson(actual);

  CHECK(actual == expected);
  CHECK(second == first);
  CHECK(first.find("\"format_version\": 1") != std::string::npos);
  CHECK(first.find("\"coefficient_si\"") != std::string::npos);
}

TEST_CASE("normalized lens prescription CSV preserves every model and RFC 4180 "
          "quoting") {
  const auto expected = makePrescription();
  const std::string first = io::serializeLensPrescriptionCsv(expected);
  const auto actual = io::parseLensPrescriptionCsv(first);
  const std::string second = io::serializeLensPrescriptionCsv(actual);

  CHECK(actual == expected);
  CHECK(second == first);
  CHECK(first.find("\"roundtrip, \"\"lens\"\"\"") != std::string::npos);
  CHECK(first.find("\"Cauchy, \"\"teaching\"\"\nmaterial\"") !=
        std::string::npos);
  CHECK(first.find("sellmeier_term,schott_n_bk7,2") != std::string::npos);
  CHECK(first.find("asphere_term,\"front,asphere\",1,6") != std::string::npos);
}

TEST_CASE(
    "lens prescription JSON and CSV file APIs round trip deterministic bytes") {
  const auto expected = makePrescription();
  const TemporaryPrescriptionFiles files;

  io::saveLensPrescriptionJson(expected, files.jsonPath());
  io::saveLensPrescriptionCsv(expected, files.csvPath());
  const std::string jsonFirst = readBytes(files.jsonPath());
  const std::string csvFirst = readBytes(files.csvPath());
  const auto jsonLoaded = io::loadLensPrescriptionJson(files.jsonPath());
  const auto csvLoaded = io::loadLensPrescriptionCsv(files.csvPath());
  io::saveLensPrescriptionJson(jsonLoaded, files.jsonPath());
  io::saveLensPrescriptionCsv(csvLoaded, files.csvPath());

  CHECK(jsonLoaded == expected);
  CHECK(csvLoaded == expected);
  CHECK(readBytes(files.jsonPath()) == jsonFirst);
  CHECK(readBytes(files.csvPath()) == csvFirst);
}

TEST_CASE("lens prescription JSON rejects versions schema drift and invalid "
          "geometry") {
  const std::string valid =
      io::serializeLensPrescriptionJson(makePrescription());
  const auto parseDiscard = [](const std::string &encoded) {
    const auto value = io::parseLensPrescriptionJson(encoded);
    static_cast<void>(value);
  };
  CHECK_THROWS_AS(parseDiscard(replacedOnce(valid, "\"format_version\": 1",
                                            "\"format_version\": 2")),
                  std::runtime_error);
  CHECK_THROWS_AS(parseDiscard(replacedOnce(
                      valid, "\"format\":", "\"unknown\": 0,\n  \"format\":")),
                  std::runtime_error);
  CHECK_THROWS_AS(parseDiscard(replacedOnce(valid, "\"radial_order\": 4",
                                            "\"radial_order\": 3")),
                  std::invalid_argument);
  CHECK_THROWS_AS(parseDiscard(replacedOnce(
                      valid,
                      "\"local_x_axis_in_world\": [\n          1.0,\n          "
                      "0.0,\n          0.0\n        ]",
                      "\"local_x_axis_in_world\": [0.5, 0.0, 0.0]")),
                  std::invalid_argument);
}

TEST_CASE("lens prescription CSV rejects malformed quoting records counts and "
          "semantics") {
  const std::string valid =
      io::serializeLensPrescriptionCsv(makePrescription());
  const auto parseDiscard = [](const std::string &encoded) {
    const auto value = io::parseLensPrescriptionCsv(encoded);
    static_cast<void>(value);
  };
  CHECK_THROWS_AS(
      parseDiscard(replacedOnce(valid, "holobench_lens_prescription_csv,1",
                                "holobench_lens_prescription_csv,2")),
      std::runtime_error);
  CHECK_THROWS_AS(
      parseDiscard(replacedOnce(valid, "sellmeier_si,3", "sellmeier_si,4")),
      std::runtime_error);
  CHECK_THROWS_AS(
      parseDiscard(replacedOnce(valid, "asphere_term,\"front,asphere\",1,6",
                                "asphere_term,\"front,asphere\",2,6")),
      std::runtime_error);
  CHECK_THROWS_AS(parseDiscard(valid + "unknown,record\n"), std::runtime_error);
  CHECK_THROWS_AS(
      parseDiscard(
          "holobench_lens_prescription_csv,1\nprescription,\"unterminated\n"),
      std::runtime_error);
  CHECK_THROWS_AS(
      parseDiscard(replacedOnce(valid, "surface,rear,-4", "surface,rear,nan")),
      std::runtime_error);
}
