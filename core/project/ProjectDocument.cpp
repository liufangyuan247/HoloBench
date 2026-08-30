#include "core/project/ProjectDocument.hpp"

#include <cmath>
#include <fstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace holobench::project {
namespace {

using Json = nlohmann::json;

Json toJson(const ComponentRecord& component) {
    for (const double coordinate : component.positionMetres) {
        if (!std::isfinite(coordinate)) {
            throw std::runtime_error("component position_m must contain only finite values");
        }
    }
    return Json {
        {"id", component.id},
        {"type", component.type},
        {"position_m", {component.positionMetres[0], component.positionMetres[1], component.positionMetres[2]}},
    };
}

ComponentRecord componentFromJson(const Json& json) {
    const auto& position = json.at("position_m");
    if (!position.is_array() || position.size() != 3) {
        throw std::runtime_error("component position_m must contain exactly three values");
    }

    ComponentRecord result;
    result.id = json.at("id").get<std::string>();
    result.type = json.at("type").get<std::string>();
    for (std::size_t index = 0; index < 3; ++index) {
        result.positionMetres[index] = position.at(index).get<double>();
    }
    return result;
}

} // namespace

void save(const ProjectDocument& project, const std::filesystem::path& path) {
    Json components = Json::array();
    for (const auto& component : project.components) {
        components.push_back(toJson(component));
    }

    const Json document {
        {"format_version", project.formatVersion},
        {"name", project.name},
        {"components", std::move(components)},
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
        ProjectDocument project;
        project.formatVersion = document.at("format_version").get<int>();
        if (project.formatVersion != kCurrentFormatVersion) {
            throw std::runtime_error("unsupported project format version: " + std::to_string(project.formatVersion));
        }
        project.name = document.at("name").get<std::string>();
        for (const auto& component : document.at("components")) {
            project.components.push_back(componentFromJson(component));
        }
        return project;
    } catch (const Json::exception& error) {
        throw std::runtime_error("invalid project '" + path.string() + "': " + error.what());
    }
}

} // namespace holobench::project
