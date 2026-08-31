#include "optics/io/LensPrescriptionIO.hpp"

#include <array>
#include <charconv>
#include <cmath>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace holobench::optics::io {
namespace {

using Json = nlohmann::json;

constexpr std::string_view kJsonFormat = "holobench-lens-prescription";
constexpr std::string_view kCsvFormat = "holobench_lens_prescription_csv";

void requireExactFields(const Json &value,
                        std::initializer_list<std::string_view> fields,
                        std::string_view context) {
  if (!value.is_object()) {
    throw std::runtime_error(std::string(context) + " must be a JSON object");
  }
  if (value.size() != fields.size()) {
    throw std::runtime_error(std::string(context) +
                             " has missing or unknown fields");
  }
  for (const std::string_view field : fields) {
    if (!value.contains(std::string(field))) {
      throw std::runtime_error(std::string(context) + " is missing '" +
                               std::string(field) + "'");
    }
  }
}

[[nodiscard]] std::string requireString(const Json &object,
                                        std::string_view field,
                                        std::string_view context) {
  const Json &value = object.at(std::string(field));
  if (!value.is_string()) {
    throw std::runtime_error(std::string(context) + " field '" +
                             std::string(field) + "' must be a string");
  }
  return value.get<std::string>();
}

[[nodiscard]] double requireFiniteNumber(const Json &object,
                                         std::string_view field,
                                         std::string_view context) {
  const Json &value = object.at(std::string(field));
  if (!value.is_number()) {
    throw std::runtime_error(std::string(context) + " field '" +
                             std::string(field) + "' must be numeric");
  }
  const double result = value.get<double>();
  if (!std::isfinite(result)) {
    throw std::runtime_error(std::string(context) + " field '" +
                             std::string(field) + "' must be finite");
  }
  return result;
}

[[nodiscard]] unsigned requireUnsigned(const Json &object,
                                       std::string_view field,
                                       std::string_view context) {
  const Json &value = object.at(std::string(field));
  if (!value.is_number_unsigned()) {
    throw std::runtime_error(std::string(context) + " field '" +
                             std::string(field) + "' must be unsigned");
  }
  const auto result = value.get<unsigned long long>();
  if (result > std::numeric_limits<unsigned>::max()) {
    throw std::runtime_error(std::string(context) +
                             " unsigned field is out of range");
  }
  return static_cast<unsigned>(result);
}

[[nodiscard]] Json vectorToJson(math::Vec3d value) {
  return Json::array({value.x, value.y, value.z});
}

[[nodiscard]] math::Vec3d vectorFromJson(const Json &value,
                                         std::string_view context) {
  if (!value.is_array() || value.size() != 3) {
    throw std::runtime_error(std::string(context) +
                             " must contain exactly three numeric values");
  }
  math::Vec3d result;
  std::array<double *, 3> coordinates{&result.x, &result.y, &result.z};
  for (std::size_t index = 0; index < coordinates.size(); ++index) {
    if (!value.at(index).is_number()) {
      throw std::runtime_error(std::string(context) +
                               " must contain exactly three numeric values");
    }
    *coordinates[index] = value.at(index).get<double>();
    if (!std::isfinite(*coordinates[index])) {
      throw std::runtime_error(std::string(context) +
                               " must contain only finite values");
    }
  }
  return result;
}

[[nodiscard]] Json materialToJson(const material::OpticalMaterial &value) {
  Json dispersion;
  std::visit(
      [&](const auto &model) {
        using Model = std::decay_t<decltype(model)>;
        if constexpr (std::is_same_v<Model, material::ConstantIndexModel>) {
          dispersion = {
              {"refractive_index", model.refractiveIndex},
              {"type", "constant"},
          };
        } else if constexpr (std::is_same_v<Model, material::CauchyModelSi>) {
          dispersion = {
              {"a", model.aDimensionless},
              {"b_m2", model.bSquareMetres},
              {"c_m4", model.cFourthMetres},
              {"type", "cauchy_si"},
          };
        } else {
          Json terms = Json::array();
          for (const material::SellmeierTermSi &term : model.terms) {
            terms.push_back({
                {"b", term.bDimensionless},
                {"c_m2", term.cSquareMetres},
            });
          }
          dispersion = {
              {"terms", std::move(terms)},
              {"type", "sellmeier_si"},
          };
        }
      },
      value.dispersion);
  return {
      {"dispersion", std::move(dispersion)},
      {"display_name", value.displayName},
      {"id", value.id},
      {"wavelength_domain_m",
       {
           {"maximum", value.wavelengthDomain.maximumMetres},
           {"minimum", value.wavelengthDomain.minimumMetres},
       }},
  };
}

[[nodiscard]] material::OpticalMaterial materialFromJson(const Json &value) {
  requireExactFields(
      value, {"dispersion", "display_name", "id", "wavelength_domain_m"},
      "material");
  const Json &domain = value.at("wavelength_domain_m");
  requireExactFields(domain, {"maximum", "minimum"},
                     "material wavelength domain");
  const Json &dispersion = value.at("dispersion");
  if (!dispersion.is_object() || !dispersion.contains("type") ||
      !dispersion.at("type").is_string()) {
    throw std::runtime_error("material dispersion requires a string type");
  }
  const std::string type = dispersion.at("type").get<std::string>();
  material::DispersionModel model;
  if (type == "constant") {
    requireExactFields(dispersion, {"refractive_index", "type"},
                       "constant dispersion");
    model = material::ConstantIndexModel{
        .refractiveIndex = requireFiniteNumber(dispersion, "refractive_index",
                                               "constant dispersion"),
    };
  } else if (type == "cauchy_si") {
    requireExactFields(dispersion, {"a", "b_m2", "c_m4", "type"},
                       "Cauchy dispersion");
    model = material::CauchyModelSi{
        .aDimensionless =
            requireFiniteNumber(dispersion, "a", "Cauchy dispersion"),
        .bSquareMetres =
            requireFiniteNumber(dispersion, "b_m2", "Cauchy dispersion"),
        .cFourthMetres =
            requireFiniteNumber(dispersion, "c_m4", "Cauchy dispersion"),
    };
  } else if (type == "sellmeier_si") {
    requireExactFields(dispersion, {"terms", "type"}, "Sellmeier dispersion");
    const Json &termsJson = dispersion.at("terms");
    if (!termsJson.is_array()) {
      throw std::runtime_error("Sellmeier terms must be an array");
    }
    material::SellmeierModelSi sellmeier;
    for (const Json &termJson : termsJson) {
      requireExactFields(termJson, {"b", "c_m2"}, "Sellmeier term");
      sellmeier.terms.push_back({
          .bDimensionless =
              requireFiniteNumber(termJson, "b", "Sellmeier term"),
          .cSquareMetres =
              requireFiniteNumber(termJson, "c_m2", "Sellmeier term"),
      });
    }
    model = std::move(sellmeier);
  } else {
    throw std::runtime_error("unknown material dispersion type: " + type);
  }
  return {
      .id = requireString(value, "id", "material"),
      .displayName = requireString(value, "display_name", "material"),
      .wavelengthDomain =
          {
              .minimumMetres = requireFiniteNumber(
                  domain, "minimum", "material wavelength domain"),
              .maximumMetres = requireFiniteNumber(
                  domain, "maximum", "material wavelength domain"),
          },
      .dispersion = std::move(model),
  };
}

[[nodiscard]] Json surfaceToJson(const ray::PrescriptionSurface &value) {
  Json asphereTerms = Json::array();
  for (const ray::EvenAsphereTerm &term : value.geometry.evenAsphereTerms) {
    asphereTerms.push_back({
        {"coefficient_si", term.coefficientSi},
        {"radial_order", term.radialOrder},
    });
  }
  return {
      {"geometry",
       {
           {"clear_semi_diameter_m", value.geometry.clearSemiDiameterMetres},
           {"conic_constant", value.geometry.conicConstant},
           {"curvature_per_m", value.geometry.curvaturePerMetre},
           {"even_asphere_terms", std::move(asphereTerms)},
       }},
      {"id", value.id},
      {"local_to_world",
       {
           {"local_x_axis_in_world",
            vectorToJson(value.localToWorld.localXAxisInWorld)},
           {"local_y_axis_in_world",
            vectorToJson(value.localToWorld.localYAxisInWorld)},
           {"local_z_axis_in_world",
            vectorToJson(value.localToWorld.localZAxisInWorld)},
           {"translation_m",
            vectorToJson(value.localToWorld.translationMetres)},
       }},
      {"material_after_id", value.materialAfterId},
      {"material_before_id", value.materialBeforeId},
  };
}

[[nodiscard]] ray::PrescriptionSurface surfaceFromJson(const Json &value) {
  requireExactFields(value,
                     {"geometry", "id", "local_to_world", "material_after_id",
                      "material_before_id"},
                     "surface");
  const Json &geometry = value.at("geometry");
  requireExactFields(geometry,
                     {"clear_semi_diameter_m", "conic_constant",
                      "curvature_per_m", "even_asphere_terms"},
                     "surface geometry");
  const Json &termsJson = geometry.at("even_asphere_terms");
  if (!termsJson.is_array()) {
    throw std::runtime_error("surface even_asphere_terms must be an array");
  }
  std::vector<ray::EvenAsphereTerm> terms;
  for (const Json &termJson : termsJson) {
    requireExactFields(termJson, {"coefficient_si", "radial_order"},
                       "even asphere term");
    terms.push_back({
        .radialOrder =
            requireUnsigned(termJson, "radial_order", "even asphere term"),
        .coefficientSi = requireFiniteNumber(termJson, "coefficient_si",
                                             "even asphere term"),
    });
  }
  const Json &transform = value.at("local_to_world");
  requireExactFields(transform,
                     {"local_x_axis_in_world", "local_y_axis_in_world",
                      "local_z_axis_in_world", "translation_m"},
                     "surface transform");
  return {
      .id = requireString(value, "id", "surface"),
      .geometry =
          {
              .curvaturePerMetre = requireFiniteNumber(
                  geometry, "curvature_per_m", "surface geometry"),
              .conicConstant = requireFiniteNumber(geometry, "conic_constant",
                                                   "surface geometry"),
              .evenAsphereTerms = std::move(terms),
              .clearSemiDiameterMetres = requireFiniteNumber(
                  geometry, "clear_semi_diameter_m", "surface geometry"),
          },
      .localToWorld =
          {
              .translationMetres = vectorFromJson(transform.at("translation_m"),
                                                  "surface translation_m"),
              .localXAxisInWorld =
                  vectorFromJson(transform.at("local_x_axis_in_world"),
                                 "surface local_x_axis_in_world"),
              .localYAxisInWorld =
                  vectorFromJson(transform.at("local_y_axis_in_world"),
                                 "surface local_y_axis_in_world"),
              .localZAxisInWorld =
                  vectorFromJson(transform.at("local_z_axis_in_world"),
                                 "surface local_z_axis_in_world"),
          },
      .materialBeforeId = requireString(value, "material_before_id", "surface"),
      .materialAfterId = requireString(value, "material_after_id", "surface"),
  };
}

[[nodiscard]] std::string formatDouble(double value) {
  if (!std::isfinite(value)) {
    throw std::invalid_argument(
        "CSV serialization requires finite numeric values");
  }
  std::array<char, 64> buffer{};
  const auto conversion = std::to_chars(
      buffer.data(), buffer.data() + buffer.size(), value,
      std::chars_format::general, std::numeric_limits<double>::max_digits10);
  if (conversion.ec != std::errc{}) {
    throw std::runtime_error("failed to format a CSV floating-point value");
  }
  return std::string(buffer.data(), conversion.ptr);
}

[[nodiscard]] std::string formatUnsigned(std::size_t value) {
  std::array<char, 32> buffer{};
  const auto conversion =
      std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
  if (conversion.ec != std::errc{}) {
    throw std::runtime_error("failed to format a CSV unsigned value");
  }
  return std::string(buffer.data(), conversion.ptr);
}

[[nodiscard]] std::string quoteCsv(std::string_view value) {
  if (value.find_first_of(",\"\r\n") == std::string_view::npos) {
    return std::string(value);
  }
  std::string result;
  result.reserve(value.size() + 2);
  result.push_back('"');
  for (const char character : value) {
    if (character == '"') {
      result.push_back('"');
    }
    result.push_back(character);
  }
  result.push_back('"');
  return result;
}

void appendCsvRow(std::string &output,
                  std::initializer_list<std::string> fields) {
  bool first = true;
  for (const std::string &field : fields) {
    if (!first) {
      output.push_back(',');
    }
    output += quoteCsv(field);
    first = false;
  }
  output.push_back('\n');
}

[[nodiscard]] std::vector<std::vector<std::string>>
parseCsvRows(std::string_view encoded) {
  std::vector<std::vector<std::string>> rows;
  std::vector<std::string> row;
  std::string field;
  bool inQuotes = false;
  bool quoteClosed = false;
  bool rowOpen = false;
  for (std::size_t index = 0; index < encoded.size(); ++index) {
    const char character = encoded[index];
    if (inQuotes) {
      if (character == '"') {
        if (index + 1 < encoded.size() && encoded[index + 1] == '"') {
          field.push_back('"');
          ++index;
        } else {
          inQuotes = false;
          quoteClosed = true;
        }
      } else {
        field.push_back(character);
      }
      continue;
    }
    if (character == '"') {
      if (!field.empty() || quoteClosed) {
        throw std::runtime_error("CSV quote must start an empty field");
      }
      inQuotes = true;
      rowOpen = true;
    } else if (character == ',') {
      row.push_back(std::move(field));
      field.clear();
      quoteClosed = false;
      rowOpen = true;
    } else if (character == '\n' || character == '\r') {
      if (character == '\r' && index + 1 < encoded.size() &&
          encoded[index + 1] == '\n') {
        ++index;
      }
      row.push_back(std::move(field));
      field.clear();
      rows.push_back(std::move(row));
      row.clear();
      quoteClosed = false;
      rowOpen = false;
    } else {
      if (quoteClosed) {
        throw std::runtime_error("CSV contains data after a closing quote");
      }
      field.push_back(character);
      rowOpen = true;
    }
  }
  if (inQuotes) {
    throw std::runtime_error("CSV contains an unterminated quoted field");
  }
  if (rowOpen || !row.empty() || !field.empty()) {
    row.push_back(std::move(field));
    rows.push_back(std::move(row));
  }
  return rows;
}

[[nodiscard]] double parseCsvDouble(std::string_view encoded,
                                    std::string_view context) {
  double value = 0.0;
  const auto conversion =
      std::from_chars(encoded.data(), encoded.data() + encoded.size(), value,
                      std::chars_format::general);
  if (conversion.ec != std::errc{} ||
      conversion.ptr != encoded.data() + encoded.size() ||
      !std::isfinite(value)) {
    throw std::runtime_error(std::string(context) +
                             " must be a finite floating-point value");
  }
  return value;
}

[[nodiscard]] std::size_t parseCsvSize(std::string_view encoded,
                                       std::string_view context) {
  std::size_t value = 0;
  const auto conversion =
      std::from_chars(encoded.data(), encoded.data() + encoded.size(), value);
  if (conversion.ec != std::errc{} ||
      conversion.ptr != encoded.data() + encoded.size()) {
    throw std::runtime_error(std::string(context) +
                             " must be an unsigned integer");
  }
  return value;
}

[[nodiscard]] unsigned parseCsvUnsigned(std::string_view encoded,
                                        std::string_view context) {
  const std::size_t value = parseCsvSize(encoded, context);
  if (value > std::numeric_limits<unsigned>::max()) {
    throw std::runtime_error(std::string(context) + " is out of range");
  }
  return static_cast<unsigned>(value);
}

void requireCsvFieldCount(const std::vector<std::string> &row,
                          std::size_t expected, std::string_view recordType) {
  if (row.size() != expected) {
    throw std::runtime_error(std::string(recordType) + " CSV record requires " +
                             std::to_string(expected) + " fields");
  }
}

[[nodiscard]] std::string readTextFile(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("unable to open lens prescription for reading: " +
                             path.string());
  }
  return std::string(std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>());
}

void writeTextFile(const std::filesystem::path &path,
                   std::string_view encoded) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) {
    throw std::runtime_error("unable to open lens prescription for writing: " +
                             path.string());
  }
  output.write(encoded.data(), static_cast<std::streamsize>(encoded.size()));
  if (!output) {
    throw std::runtime_error("failed while writing lens prescription: " +
                             path.string());
  }
}

} // namespace

std::string serializeLensPrescriptionJson(
    const ray::SequentialLensPrescription &prescription) {
  ray::validateSequentialLensPrescription(prescription);
  Json materials = Json::array();
  for (const material::OpticalMaterial &value : prescription.materials) {
    materials.push_back(materialToJson(value));
  }
  Json surfaces = Json::array();
  for (const ray::PrescriptionSurface &value : prescription.surfaces) {
    surfaces.push_back(surfaceToJson(value));
  }
  const Json document{
      {"format", kJsonFormat},
      {"format_version", kLensPrescriptionJsonFormatVersion},
      {"id", prescription.id},
      {"materials", std::move(materials)},
      {"surfaces", std::move(surfaces)},
  };
  return document.dump(2) + '\n';
}

ray::SequentialLensPrescription
parseLensPrescriptionJson(std::string_view encoded) {
  try {
    const Json document = Json::parse(encoded);
    requireExactFields(
        document, {"format", "format_version", "id", "materials", "surfaces"},
        "lens prescription document");
    if (!document.at("format").is_string() ||
        document.at("format").get<std::string>() != kJsonFormat) {
      throw std::runtime_error(
          "invalid lens prescription JSON format identifier");
    }
    if (!document.at("format_version").is_number_integer()) {
      throw std::runtime_error(
          "lens prescription JSON format_version must be an integer");
    }
    const int version = document.at("format_version").get<int>();
    if (version != kLensPrescriptionJsonFormatVersion) {
      throw std::runtime_error(
          "unsupported lens prescription JSON format version: " +
          std::to_string(version));
    }
    if (!document.at("materials").is_array() ||
        !document.at("surfaces").is_array()) {
      throw std::runtime_error(
          "lens prescription materials and surfaces must be arrays");
    }
    ray::SequentialLensPrescription result;
    result.id = requireString(document, "id", "lens prescription document");
    for (const Json &value : document.at("materials")) {
      result.materials.push_back(materialFromJson(value));
    }
    for (const Json &value : document.at("surfaces")) {
      result.surfaces.push_back(surfaceFromJson(value));
    }
    ray::validateSequentialLensPrescription(result);
    return result;
  } catch (const Json::exception &error) {
    throw std::runtime_error(std::string("invalid lens prescription JSON: ") +
                             error.what());
  }
}

void saveLensPrescriptionJson(
    const ray::SequentialLensPrescription &prescription,
    const std::filesystem::path &path) {
  writeTextFile(path, serializeLensPrescriptionJson(prescription));
}

ray::SequentialLensPrescription
loadLensPrescriptionJson(const std::filesystem::path &path) {
  return parseLensPrescriptionJson(readTextFile(path));
}

std::string serializeLensPrescriptionCsv(
    const ray::SequentialLensPrescription &prescription) {
  ray::validateSequentialLensPrescription(prescription);
  std::string output;
  appendCsvRow(output, {std::string(kCsvFormat),
                        std::to_string(kLensPrescriptionCsvFormatVersion)});
  appendCsvRow(output, {"prescription", prescription.id});
  for (const material::OpticalMaterial &value : prescription.materials) {
    std::visit(
        [&](const auto &model) {
          using Model = std::decay_t<decltype(model)>;
          if constexpr (std::is_same_v<Model, material::ConstantIndexModel>) {
            appendCsvRow(output,
                         {
                             "material",
                             value.id,
                             value.displayName,
                             formatDouble(value.wavelengthDomain.minimumMetres),
                             formatDouble(value.wavelengthDomain.maximumMetres),
                             "constant",
                             formatDouble(model.refractiveIndex),
                         });
          } else if constexpr (std::is_same_v<Model, material::CauchyModelSi>) {
            appendCsvRow(output,
                         {
                             "material",
                             value.id,
                             value.displayName,
                             formatDouble(value.wavelengthDomain.minimumMetres),
                             formatDouble(value.wavelengthDomain.maximumMetres),
                             "cauchy_si",
                             formatDouble(model.aDimensionless),
                             formatDouble(model.bSquareMetres),
                             formatDouble(model.cFourthMetres),
                         });
          } else {
            appendCsvRow(output,
                         {
                             "material",
                             value.id,
                             value.displayName,
                             formatDouble(value.wavelengthDomain.minimumMetres),
                             formatDouble(value.wavelengthDomain.maximumMetres),
                             "sellmeier_si",
                             formatUnsigned(model.terms.size()),
                         });
            for (std::size_t index = 0; index < model.terms.size(); ++index) {
              appendCsvRow(output,
                           {
                               "sellmeier_term",
                               value.id,
                               formatUnsigned(index),
                               formatDouble(model.terms[index].bDimensionless),
                               formatDouble(model.terms[index].cSquareMetres),
                           });
            }
          }
        },
        value.dispersion);
  }
  for (const ray::PrescriptionSurface &value : prescription.surfaces) {
    const auto &transform = value.localToWorld;
    appendCsvRow(output,
                 {
                     "surface",
                     value.id,
                     formatDouble(value.geometry.curvaturePerMetre),
                     formatDouble(value.geometry.conicConstant),
                     formatDouble(value.geometry.clearSemiDiameterMetres),
                     formatDouble(transform.translationMetres.x),
                     formatDouble(transform.translationMetres.y),
                     formatDouble(transform.translationMetres.z),
                     formatDouble(transform.localXAxisInWorld.x),
                     formatDouble(transform.localXAxisInWorld.y),
                     formatDouble(transform.localXAxisInWorld.z),
                     formatDouble(transform.localYAxisInWorld.x),
                     formatDouble(transform.localYAxisInWorld.y),
                     formatDouble(transform.localYAxisInWorld.z),
                     formatDouble(transform.localZAxisInWorld.x),
                     formatDouble(transform.localZAxisInWorld.y),
                     formatDouble(transform.localZAxisInWorld.z),
                     value.materialBeforeId,
                     value.materialAfterId,
                     formatUnsigned(value.geometry.evenAsphereTerms.size()),
                 });
    for (std::size_t index = 0; index < value.geometry.evenAsphereTerms.size();
         ++index) {
      const ray::EvenAsphereTerm &term = value.geometry.evenAsphereTerms[index];
      appendCsvRow(output, {
                               "asphere_term",
                               value.id,
                               formatUnsigned(index),
                               formatUnsigned(term.radialOrder),
                               formatDouble(term.coefficientSi),
                           });
    }
  }
  return output;
}

ray::SequentialLensPrescription
parseLensPrescriptionCsv(std::string_view encoded) {
  const auto rows = parseCsvRows(encoded);
  if (rows.empty()) {
    throw std::runtime_error("lens prescription CSV is empty");
  }
  requireCsvFieldCount(rows.front(), 2, "format");
  if (rows.front()[0] != kCsvFormat) {
    throw std::runtime_error("invalid lens prescription CSV format identifier");
  }
  const std::size_t version =
      parseCsvSize(rows.front()[1], "CSV format version");
  if (version != static_cast<std::size_t>(kLensPrescriptionCsvFormatVersion)) {
    throw std::runtime_error(
        "unsupported lens prescription CSV format version: " +
        std::to_string(version));
  }

  struct PendingCount final {
    std::size_t objectIndex = 0;
    std::size_t expectedTerms = 0;
  };
  ray::SequentialLensPrescription result;
  bool havePrescription = false;
  std::unordered_map<std::string, PendingCount> sellmeierMaterials;
  std::unordered_map<std::string, PendingCount> asphereSurfaces;
  std::unordered_map<std::string, bool> materialIds;
  std::unordered_map<std::string, bool> surfaceIds;

  for (std::size_t rowIndex = 1; rowIndex < rows.size(); ++rowIndex) {
    const auto &row = rows[rowIndex];
    if (row.empty() || row[0].empty()) {
      throw std::runtime_error(
          "lens prescription CSV contains an empty record");
    }
    const std::string &type = row[0];
    if (type == "prescription") {
      requireCsvFieldCount(row, 2, "prescription");
      if (havePrescription) {
        throw std::runtime_error(
            "lens prescription CSV contains multiple prescription records");
      }
      result.id = row[1];
      havePrescription = true;
    } else if (type == "material") {
      if (row.size() < 6) {
        throw std::runtime_error("material CSV record has too few fields");
      }
      if (!materialIds.emplace(row[1], true).second) {
        throw std::runtime_error(
            "lens prescription CSV contains a duplicate material id");
      }
      material::OpticalMaterial value{
          .id = row[1],
          .displayName = row[2],
          .wavelengthDomain =
              {
                  .minimumMetres =
                      parseCsvDouble(row[3], "material minimum wavelength"),
                  .maximumMetres =
                      parseCsvDouble(row[4], "material maximum wavelength"),
              },
          .dispersion = material::ConstantIndexModel{},
      };
      if (row[5] == "constant") {
        requireCsvFieldCount(row, 7, "constant material");
        value.dispersion = material::ConstantIndexModel{
            .refractiveIndex =
                parseCsvDouble(row[6], "constant refractive index"),
        };
      } else if (row[5] == "cauchy_si") {
        requireCsvFieldCount(row, 9, "Cauchy material");
        value.dispersion = material::CauchyModelSi{
            .aDimensionless = parseCsvDouble(row[6], "Cauchy A coefficient"),
            .bSquareMetres = parseCsvDouble(row[7], "Cauchy B coefficient"),
            .cFourthMetres = parseCsvDouble(row[8], "Cauchy C coefficient"),
        };
      } else if (row[5] == "sellmeier_si") {
        requireCsvFieldCount(row, 7, "Sellmeier material");
        const std::size_t expected =
            parseCsvSize(row[6], "Sellmeier term count");
        value.dispersion = material::SellmeierModelSi{};
        sellmeierMaterials.emplace(
            value.id, PendingCount{result.materials.size(), expected});
      } else {
        throw std::runtime_error("unknown material CSV dispersion type: " +
                                 row[5]);
      }
      result.materials.push_back(std::move(value));
    } else if (type == "sellmeier_term") {
      requireCsvFieldCount(row, 5, "Sellmeier term");
      const auto pending = sellmeierMaterials.find(row[1]);
      if (pending == sellmeierMaterials.end()) {
        throw std::runtime_error(
            "Sellmeier term references an unknown or non-Sellmeier material");
      }
      auto &model = std::get<material::SellmeierModelSi>(
          result.materials[pending->second.objectIndex].dispersion);
      const std::size_t index = parseCsvSize(row[2], "Sellmeier term index");
      if (index != model.terms.size()) {
        throw std::runtime_error(
            "Sellmeier term indices must be contiguous and ordered from zero");
      }
      model.terms.push_back({
          .bDimensionless = parseCsvDouble(row[3], "Sellmeier B coefficient"),
          .cSquareMetres = parseCsvDouble(row[4], "Sellmeier C coefficient"),
      });
    } else if (type == "surface") {
      requireCsvFieldCount(row, 20, "surface");
      if (!surfaceIds.emplace(row[1], true).second) {
        throw std::runtime_error(
            "lens prescription CSV contains a duplicate surface id");
      }
      const std::size_t expected = parseCsvSize(row[19], "asphere term count");
      result.surfaces.push_back({
          .id = row[1],
          .geometry =
              {
                  .curvaturePerMetre =
                      parseCsvDouble(row[2], "surface curvature"),
                  .conicConstant =
                      parseCsvDouble(row[3], "surface conic constant"),
                  .evenAsphereTerms = {},
                  .clearSemiDiameterMetres =
                      parseCsvDouble(row[4], "surface clear semi-diameter"),
              },
          .localToWorld =
              {
                  .translationMetres =
                      {
                          parseCsvDouble(row[5], "surface translation x"),
                          parseCsvDouble(row[6], "surface translation y"),
                          parseCsvDouble(row[7], "surface translation z"),
                      },
                  .localXAxisInWorld =
                      {
                          parseCsvDouble(row[8], "surface local X axis x"),
                          parseCsvDouble(row[9], "surface local X axis y"),
                          parseCsvDouble(row[10], "surface local X axis z"),
                      },
                  .localYAxisInWorld =
                      {
                          parseCsvDouble(row[11], "surface local Y axis x"),
                          parseCsvDouble(row[12], "surface local Y axis y"),
                          parseCsvDouble(row[13], "surface local Y axis z"),
                      },
                  .localZAxisInWorld =
                      {
                          parseCsvDouble(row[14], "surface local Z axis x"),
                          parseCsvDouble(row[15], "surface local Z axis y"),
                          parseCsvDouble(row[16], "surface local Z axis z"),
                      },
              },
          .materialBeforeId = row[17],
          .materialAfterId = row[18],
      });
      asphereSurfaces.emplace(
          row[1], PendingCount{result.surfaces.size() - 1, expected});
    } else if (type == "asphere_term") {
      requireCsvFieldCount(row, 5, "asphere term");
      const auto pending = asphereSurfaces.find(row[1]);
      if (pending == asphereSurfaces.end()) {
        throw std::runtime_error("asphere term references an unknown surface");
      }
      auto &terms = result.surfaces[pending->second.objectIndex]
                        .geometry.evenAsphereTerms;
      const std::size_t index = parseCsvSize(row[2], "asphere term index");
      if (index != terms.size()) {
        throw std::runtime_error(
            "asphere term indices must be contiguous and ordered from zero");
      }
      terms.push_back({
          .radialOrder = parseCsvUnsigned(row[3], "asphere radial order"),
          .coefficientSi = parseCsvDouble(row[4], "asphere coefficient"),
      });
    } else {
      throw std::runtime_error("unknown lens prescription CSV record type: " +
                               type);
    }
  }
  if (!havePrescription) {
    throw std::runtime_error(
        "lens prescription CSV is missing the prescription record");
  }
  for (const auto &[id, pending] : sellmeierMaterials) {
    const auto &model = std::get<material::SellmeierModelSi>(
        result.materials[pending.objectIndex].dispersion);
    if (model.terms.size() != pending.expectedTerms) {
      throw std::runtime_error(
          "Sellmeier term count does not match material declaration: " + id);
    }
  }
  for (const auto &[id, pending] : asphereSurfaces) {
    if (result.surfaces[pending.objectIndex].geometry.evenAsphereTerms.size() !=
        pending.expectedTerms) {
      throw std::runtime_error(
          "asphere term count does not match surface declaration: " + id);
    }
  }
  ray::validateSequentialLensPrescription(result);
  return result;
}

void saveLensPrescriptionCsv(
    const ray::SequentialLensPrescription &prescription,
    const std::filesystem::path &path) {
  writeTextFile(path, serializeLensPrescriptionCsv(prescription));
}

ray::SequentialLensPrescription
loadLensPrescriptionCsv(const std::filesystem::path &path) {
  return parseLensPrescriptionCsv(readTextFile(path));
}

} // namespace holobench::optics::io
