#include <doctest/doctest.h>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>

#include "core/project/ProjectDocument.hpp"

namespace project = holobench::project;

namespace {

class TemporaryFile final {
public:
    explicit TemporaryFile(std::string_view stem) {
        const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path()
            / (std::string(stem) + std::to_string(unique) + ".json");
    }

    ~TemporaryFile() {
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

} // namespace

TEST_CASE("project JSON round trip is lossless") {
    project::ProjectDocument expected;
    expected.name = "M0 round trip";
    expected.components.push_back({"lens-1", "thin_lens", {0.0, 0.0, 0.25}});

    const TemporaryFile firstFile("holobench-project-");
    const TemporaryFile secondFile("holobench-project-resaved-");

    project::save(expected, firstFile.path());
    const auto actual = project::load(firstFile.path());
    project::save(actual, secondFile.path());

    CHECK(actual == expected);
    std::ifstream firstInput(firstFile.path(), std::ios::binary);
    std::ifstream secondInput(secondFile.path(), std::ios::binary);
    const std::string firstBytes((std::istreambuf_iterator<char>(firstInput)), std::istreambuf_iterator<char>());
    const std::string secondBytes((std::istreambuf_iterator<char>(secondInput)), std::istreambuf_iterator<char>());
    CHECK(firstBytes == secondBytes);
}

TEST_CASE("unsupported project versions fail explicitly") {
    const TemporaryFile file("holobench-version-");

    project::ProjectDocument future;
    future.formatVersion = project::kCurrentFormatVersion + 1;
    project::save(future, file.path());

    CHECK_THROWS_WITH_AS(static_cast<void>(project::load(file.path())),
        "unsupported project format version: 2",
        std::runtime_error);
}

TEST_CASE("malformed project JSON fails instead of producing partial state") {
    const TemporaryFile file("holobench-malformed-");
    {
        std::ofstream output(file.path());
        output << R"({"format_version":1,"name":"broken","components":[)";
    }

    CHECK_THROWS_AS(static_cast<void>(project::load(file.path())), std::runtime_error);
}

TEST_CASE("non-finite component coordinates are rejected") {
    const TemporaryFile file("holobench-non-finite-");
    project::ProjectDocument document;
    document.components.push_back({"bad", "screen", {0.0, std::nan(""), 0.0}});
    CHECK_THROWS_WITH_AS(project::save(document, file.path()),
        "component position_m must contain only finite values",
        std::runtime_error);
}
