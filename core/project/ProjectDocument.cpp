#include "core/project/ProjectDocument.hpp"

#include <cmath>
#include <fstream>
#include <stdexcept>
#include <unordered_set>

#include <nlohmann/json.hpp>

namespace holobench::project {
namespace {

using Json = nlohmann::json;

Json toJson(const ComponentRecord& component) {
    if (component.id.empty()) {
        throw std::runtime_error("component id must not be empty");
    }
    if (component.type.empty()) {
        throw std::runtime_error("component type must not be empty");
    }
    for (const double coordinate : component.positionMetres) {
        if (!std::isfinite(coordinate)) {
            throw std::runtime_error("component position_m must contain only finite values");
        }
    }
    for (const auto& [key, value] : component.scalarParameters) {
        if (key.empty()) {
            throw std::runtime_error("component parameter key must not be empty");
        }
        if (!std::isfinite(value)) {
            throw std::runtime_error("component parameter '" + key + "' must be finite");
        }
    }

    Json json = Json::object();
    json["id"] = component.id;
    json["parameters"] = component.scalarParameters;
    json["position_m"] = {component.positionMetres[0], component.positionMetres[1], component.positionMetres[2]};
    json["type"] = component.type;
    return json;
}

ComponentRecord componentFromJson(const Json& json) {
    if (!json.is_object()) {
        throw std::runtime_error("component must be a JSON object");
    }
    if (!json.contains("id") || !json.at("id").is_string()) {
        throw std::runtime_error("component id must be a string");
    }
    const auto id = json.at("id").get<std::string>();
    if (id.empty()) {
        throw std::runtime_error("component id must not be empty");
    }

    if (!json.contains("type") || !json.at("type").is_string()) {
        throw std::runtime_error("component type must be a string");
    }
    const auto type = json.at("type").get<std::string>();
    if (type.empty()) {
        throw std::runtime_error("component type must not be empty");
    }

    if (!json.contains("position_m")) {
        throw std::runtime_error("component missing position_m");
    }
    const auto& position = json.at("position_m");
    if (!position.is_array() || position.size() != 3) {
        throw std::runtime_error("component position_m must contain exactly three values");
    }

    ComponentRecord result;
    result.id = id;
    result.type = type;

    for (std::size_t index = 0; index < 3; ++index) {
        if (!position.at(index).is_number()) {
            throw std::runtime_error("component position_m must contain numeric values");
        }
        const double coord = position.at(index).get<double>();
        if (!std::isfinite(coord)) {
            throw std::runtime_error("component position_m must contain only finite values");
        }
        result.positionMetres[index] = coord;
    }

    if (json.contains("parameters")) {
        const auto& params = json.at("parameters");
        if (!params.is_object()) {
            throw std::runtime_error("component parameters must be an object");
        }
        for (auto it = params.begin(); it != params.end(); ++it) {
            if (it.key().empty()) {
                throw std::runtime_error("component parameter key must not be empty");
            }
            if (!it.value().is_number()) {
                throw std::runtime_error("component parameter '" + it.key() + "' must be numeric");
            }
            const double paramVal = it.value().get<double>();
            if (!std::isfinite(paramVal)) {
                throw std::runtime_error("component parameter '" + it.key() + "' must be finite");
            }
            result.scalarParameters[it.key()] = paramVal;
        }
    }

    return result;
}

} // namespace

void save(const ProjectDocument& project, const std::filesystem::path& path) {
    std::unordered_set<std::string> seenIds;
    Json components = Json::array();
    for (const auto& component : project.components) {
        if (component.id.empty()) {
            throw std::runtime_error("component id must not be empty");
        }
        if (!seenIds.insert(component.id).second) {
            throw std::runtime_error("duplicate component id: " + component.id);
        }
        components.push_back(toJson(component));
    }

    const Json document {
        {"components", std::move(components)},
        {"format_version", project.formatVersion},
        {"name", project.name},
    };

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("unable to open project for writing: " + path.string());
    }
    output << document.dump(2) << '\n';
    if (!output) {
        throw std::runtime_error("failed while writing project: " + path.string());
    }
}

ProjectDocument load(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("unable to open project for reading: " + path.string());
    }

    try {
        const Json document = Json::parse(input);
        if (!document.is_object()) {
            throw std::runtime_error("project document must be a JSON object");
        }
        if (!document.contains("format_version") || !document.at("format_version").is_number_integer()) {
            throw std::runtime_error("project missing format_version");
        }
        ProjectDocument project;
        project.formatVersion = document.at("format_version").get<int>();
        if (project.formatVersion != kCurrentFormatVersion) {
            throw std::runtime_error("unsupported project format version: " + std::to_string(project.formatVersion));
        }
        if (!document.contains("name") || !document.at("name").is_string()) {
            throw std::runtime_error("project missing or invalid name");
        }
        project.name = document.at("name").get<std::string>();

        if (!document.contains("components") || !document.at("components").is_array()) {
            throw std::runtime_error("project missing or invalid components array");
        }

        std::unordered_set<std::string> seenIds;
        for (const auto& compJson : document.at("components")) {
            auto comp = componentFromJson(compJson);
            if (!seenIds.insert(comp.id).second) {
                throw std::runtime_error("duplicate component id: " + comp.id);
            }
            project.components.push_back(std::move(comp));
        }
        return project;
    } catch (const Json::exception& error) {
        throw std::runtime_error("invalid project '" + path.string() + "': " + error.what());
    }
}

} // namespace holobench::project
