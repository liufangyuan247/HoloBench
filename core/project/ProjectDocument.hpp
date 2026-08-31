#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "core/project/ProjectProvenance.hpp"

namespace holobench::project {

inline constexpr int kLegacyFormatVersion = 1;
inline constexpr int kCurrentFormatVersion = 2;

struct ComponentRecord final {
    std::string id;
    std::string type;
    double positionMetres[3] {0.0, 0.0, 0.0};
    std::map<std::string, double> scalarParameters {};

    bool operator==(const ComponentRecord&) const = default;
};

struct ProjectDocument final {
    int formatVersion = kCurrentFormatVersion;
    std::string name = "Untitled";
    ProjectProvenance provenance;
    std::vector<ComponentRecord> components;

    bool operator==(const ProjectDocument&) const = default;
};

void save(const ProjectDocument& project, const std::filesystem::path& path);
[[nodiscard]] ProjectDocument load(const std::filesystem::path& path);

} // namespace holobench::project
