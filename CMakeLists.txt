cmake_minimum_required(VERSION 3.28)

project(HoloBench VERSION 0.1.0 LANGUAGES C CXX)

include(CTest)
include(FetchContent)

option(HOLOBENCH_BUILD_APP "Build the interactive HoloBench application" ON)
option(HOLOBENCH_BUILD_TESTS "Build deterministic core tests" ON)
option(HOLOBENCH_WARNINGS_AS_ERRORS "Treat project warnings as errors" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

function(holobench_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
        if(HOLOBENCH_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
        if(HOLOBENCH_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()

FetchContent_Declare(
    nlohmann_json
    URL https://codeload.github.com/nlohmann/json/tar.gz/65ee68451d8eb2b5f3a30b410476ab83deb3289b
    URL_HASH SHA256=13ef31d691947940a08909f8e0772f1d7d68e5da1678ee812a49c4bb0c996b2f
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE
)
FetchContent_MakeAvailable(nlohmann_json)

add_library(holobench_core STATIC
    core/field/ComplexField2D.cpp
    core/field/FieldObservables.cpp
    core/field/FieldVisualization.cpp
    core/field/ScalarField2D.cpp
    core/project/ProjectDocument.cpp
)
add_library(HoloBench::Core ALIAS holobench_core)
target_include_directories(holobench_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(holobench_core PUBLIC nlohmann_json::nlohmann_json)
target_compile_features(holobench_core PUBLIC cxx_std_20)
holobench_set_warnings(holobench_core)

add_library(holobench_compute STATIC
    compute/fft/CpuFftBackend.cpp
    compute/propagation/AngularSpectrumPropagator.cpp
    compute/propagation/FraunhoferPropagator.cpp
    compute/propagation/FresnelPropagator.cpp
)
add_library(HoloBench::Compute ALIAS holobench_compute)
target_include_directories(holobench_compute PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(holobench_compute PUBLIC HoloBench::Core)
target_compile_features(holobench_compute PUBLIC cxx_std_20)
holobench_set_warnings(holobench_compute)

add_library(holobench_optics STATIC
    optics/ray/BenchTracer.cpp
    optics/ray/GeometricElements.cpp
    optics/ray/Interface.cpp
    optics/ray/Ray.cpp
    optics/ray/ThinLens.cpp
    optics/scene/GeometricComponents.cpp
    optics/scene/NumericalAperture.cpp
    optics/scene/OpticalBenchScene.cpp
    optics/scene/SceneProjectAdapter.cpp
    optics/wave/FieldElements.cpp
    optics/wave/FieldSources.cpp
)
add_library(HoloBench::Optics ALIAS holobench_optics)
target_include_directories(holobench_optics PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(holobench_optics PUBLIC HoloBench::Core)
target_compile_features(holobench_optics PUBLIC cxx_std_20)
holobench_set_warnings(holobench_optics)

# Headless application-use-case orchestration. This layer composes the
# solver-independent optical models with numerical backends without making
# either lower-level library depend on the other implementation layer.
add_library(holobench_app_logic STATIC
    app/WaveDetectorPipeline.cpp
    app/WaveDetectorUiState.cpp
)
add_library(HoloBench::AppLogic ALIAS holobench_app_logic)
target_include_directories(holobench_app_logic PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(holobench_app_logic PUBLIC
    HoloBench::Core
    HoloBench::Compute
    HoloBench::Optics
)
target_compile_features(holobench_app_logic PUBLIC cxx_std_20)
holobench_set_warnings(holobench_app_logic)

set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLM_BUILD_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    glm
    URL https://codeload.github.com/g-truc/glm/tar.gz/a532f5b1cf27d6a3c099437e6959cf7e398a0a67
    URL_HASH SHA256=3a83ca4c2dba0497af5f8c09a9cc90b03a80c7012bd56920afaab4acfa09f634
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE
)
FetchContent_MakeAvailable(glm)

if(HOLOBENCH_BUILD_APP)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        SDL3
        URL https://codeload.github.com/libsdl-org/SDL/tar.gz/f5e5f6588921eed3d7d048ce43d9eb1ff0da0ffc
        URL_HASH SHA256=7f80bce51b7179bab472e75b90ce8ac06b162b3da8e19feadecee5acea07dbfb
        DOWNLOAD_EXTRACT_TIMESTAMP FALSE
    )
    FetchContent_MakeAvailable(SDL3)

    FetchContent_Declare(
        imgui
        URL https://codeload.github.com/ocornut/imgui/tar.gz/b48d1afbe8ee8b238e2961dc363a949dd7304e23
        URL_HASH SHA256=747c5a465a05c4ef5b4a09e2e0e3d0ce2bc76b31420f0f5af33df470907cf893
        DOWNLOAD_EXTRACT_TIMESTAMP FALSE
    )
    FetchContent_MakeAvailable(imgui)

    FetchContent_Declare(
        glad
        URL https://codeload.github.com/Dav1dde/glad/tar.gz/73db193f853e2ee079bf3ca8a64aa2eaf6459043
        URL_HASH SHA256=33dbeae44d8315ece57e14eba4b1b4a02ac5406d1c4f49cd20048c91522a6b9a
        DOWNLOAD_EXTRACT_TIMESTAMP FALSE
        SOURCE_SUBDIR cmake
    )
    FetchContent_MakeAvailable(glad)
    glad_add_library(holobench_glad STATIC REPRODUCIBLE API gl:core=4.6)

    find_package(OpenGL REQUIRED)

    add_library(holobench_imgui STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_demo.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    )
    target_include_directories(holobench_imgui SYSTEM PUBLIC
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
    )
    target_link_libraries(holobench_imgui PUBLIC SDL3::SDL3-static OpenGL::GL)
    target_compile_features(holobench_imgui PUBLIC cxx_std_20)

    add_executable(holobench_app
        app/main.cpp
        app/Application.cpp
        render/Camera.cpp
        render/gl/Shader.cpp
        render/gl/Framebuffer.cpp
        render/gl/Texture2D.cpp
        render/OpticalBenchRenderer.cpp
        render/gl/GlDebug.cpp
    )
    set_target_properties(holobench_app PROPERTIES OUTPUT_NAME HoloBench)
    target_include_directories(holobench_app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(holobench_app PRIVATE
        HoloBench::Core
        HoloBench::AppLogic
        HoloBench::Compute
        HoloBench::Optics
        holobench_imgui
        holobench_glad
        glm::glm
        SDL3::SDL3-static
        OpenGL::GL
    )
    holobench_set_warnings(holobench_app)
endif()

if(BUILD_TESTING AND HOLOBENCH_BUILD_TESTS)
    FetchContent_Declare(
        doctest
        URL https://codeload.github.com/doctest/doctest/tar.gz/2d0a9359a60c51affe2a9bebb1be1dca47868151
        URL_HASH SHA256=e64542c4ea68e9f381ccf6eae924cfdd652567c87c142d76fe92644fb4608149
        DOWNLOAD_EXTRACT_TIMESTAMP FALSE
    )
    FetchContent_MakeAvailable(doctest)

    add_executable(holobench_tests
        tests/test_main.cpp
        tests/core/UnitsTests.cpp
        tests/core/ComplexFieldTests.cpp
        tests/core/FieldObservablesTests.cpp
        tests/core/FieldVisualizationTests.cpp
        tests/compute/AngularSpectrumPropagatorTests.cpp
        tests/compute/CpuFftBackendTests.cpp
        tests/compute/FraunhoferPropagatorTests.cpp
        tests/compute/FresnelPropagatorTests.cpp
        tests/analytic/BenchTracerTests.cpp
        tests/analytic/GeometricElementsTests.cpp
        tests/analytic/FieldElementTests.cpp
        tests/analytic/FraunhoferDiffractionTests.cpp
        tests/analytic/NumericalApertureTests.cpp
        tests/analytic/SnellTests.cpp
        tests/analytic/ThinLensTests.cpp
        tests/analytic/WaveDetectorPipelineTests.cpp
        tests/analytic/WaveSourceTests.cpp
        tests/project/ProjectDocumentTests.cpp
        tests/project/SceneProjectAdapterTests.cpp
        tests/validation/WavepropCrossValidationTests.cpp
        tests/app/WaveDetectorUiStateTests.cpp
        render/Camera.cpp
        tests/render/CameraTests.cpp
        tests/render/GizmoTests.cpp
    )
    target_link_libraries(holobench_tests PRIVATE HoloBench::Core HoloBench::AppLogic HoloBench::Compute HoloBench::Optics doctest::doctest glm::glm)
    set(HOLOBENCH_WAVEPROP_GOLDEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/validation/waveprop/goldens")
    file(COPY
        "${CMAKE_CURRENT_SOURCE_DIR}/validation/waveprop/goldens/"
        DESTINATION "${HOLOBENCH_WAVEPROP_GOLDEN_DIR}"
    )
    target_compile_definitions(holobench_tests PRIVATE
        HOLOBENCH_WAVEPROP_GOLDEN_DIR="${HOLOBENCH_WAVEPROP_GOLDEN_DIR}"
    )
    holobench_set_warnings(holobench_tests)

    include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)
    doctest_discover_tests(holobench_tests)
endif()
