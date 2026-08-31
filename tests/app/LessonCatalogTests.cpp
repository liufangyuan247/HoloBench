#include <doctest/doctest.h>

#include <stdexcept>
#include <vector>

#include "app/lessons/LessonCatalog.hpp"

namespace lessons = holobench::app::lessons;

namespace {

[[nodiscard]] lessons::LessonStep testStep(std::string id = "step") {
    return {
        .id = std::move(id),
        .titleKey = "test.step.title",
        .instructionKey = "test.step.instruction",
        .contextKey = "test.step.context",
    };
}

[[nodiscard]] lessons::LessonDefinition testLesson(
    std::string id,
    std::vector<std::string> prerequisites = {}) {
    return {
        .id = std::move(id),
        .titleKey = "test.lesson.title",
        .objectiveKey = "test.lesson.objective",
        .projectTemplateId = "test_template",
        .prerequisiteIds = std::move(prerequisites),
        .steps = {testStep()},
    };
}

} // namespace

TEST_SUITE("LessonCatalog") {

TEST_CASE("default catalog defines ten stable lessons and eight core courses") {
    const auto catalog = lessons::makeDefaultLessonCatalog();
    REQUIRE(catalog.lessons().size() == 10U);
    CHECK(catalog.lessons().front().id == "reflection_refraction");
    CHECK(catalog.lessons()[7].id == "coherence_interference");
    CHECK_FALSE(catalog.lessons()[7].advanced);
    CHECK(catalog.lessons()[8].advanced);
    CHECK(catalog.lessons()[9].id == "h1_h2_advanced");
    for (const auto& definition : catalog.lessons()) {
        CHECK(lessons::isStableLessonKey(definition.id));
        CHECK(lessons::isStableLessonKey(definition.titleKey));
        CHECK(lessons::isStableLessonKey(definition.projectTemplateId));
        CHECK(definition.steps.size() == 3U);
    }
}

TEST_CASE("catalog lookup is explicit for unknown IDs") {
    const auto catalog = lessons::makeDefaultLessonCatalog();
    CHECK(catalog.contains("thin_lens"));
    CHECK_FALSE(catalog.contains("missing"));
    CHECK(catalog.lesson("thin_lens").prerequisiteIds
        == std::vector<std::string> {"reflection_refraction"});
    CHECK_THROWS_AS(static_cast<void>(catalog.lesson("missing")), std::invalid_argument);
}

TEST_CASE("catalog rejects duplicate lessons prerequisites steps and unstable keys") {
    CHECK_THROWS_AS(
        lessons::LessonCatalog({testLesson("one"), testLesson("one")}),
        std::invalid_argument);
    CHECK_THROWS_AS(
        lessons::LessonCatalog({testLesson("one", {"missing"})}),
        std::invalid_argument);

    auto duplicatePrerequisite = testLesson("two", {"one", "one"});
    CHECK_THROWS_AS(
        lessons::LessonCatalog({testLesson("one"), duplicatePrerequisite}),
        std::invalid_argument);

    auto duplicateStep = testLesson("one");
    duplicateStep.steps.push_back(testStep());
    CHECK_THROWS_AS(lessons::LessonCatalog({duplicateStep}), std::invalid_argument);

    auto unstable = testLesson("Localized Title");
    CHECK_THROWS_AS(lessons::LessonCatalog({unstable}), std::invalid_argument);
    CHECK_FALSE(lessons::isStableLessonKey("bad..key"));
    CHECK_FALSE(lessons::isStableLessonKey("BadKey"));
}

TEST_CASE("catalog rejects direct and indirect prerequisite cycles") {
    CHECK_THROWS_AS(
        lessons::LessonCatalog({testLesson("one", {"one"})}),
        std::invalid_argument);
    CHECK_THROWS_AS(
        lessons::LessonCatalog({
            testLesson("one", {"three"}),
            testLesson("two", {"one"}),
            testLesson("three", {"two"}),
        }),
        std::invalid_argument);
}

} // TEST_SUITE("LessonCatalog")
