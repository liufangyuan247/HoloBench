cmake_minimum_required(VERSION 3.28)

project(HoloBench VERSION 0.1.0 LANGUAGES CXX)

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
        target_compile_options(${target} PRIVATE /W4 /permissive-)
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
    core/project/ProjectDocument.cpp
)
add_library(HoloBench::Core ALIAS holobench_core)
target_include_directories(holobench_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(holobench_core PUBLIC nlohmann_json::nlohmann_json)
target_compile_features(holobench_core PUBLIC cxx_std_20)
holobench_set_warnings(holobench_core)

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
    target_include_directories(holobench_imgui PUBLIC
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
    )
    target_link_libraries(holobench_imgui PUBLIC SDL3::SDL3-static OpenGL::GL)
    target_compile_features(holobench_imgui PUBLIC cxx_std_20)

    add_executable(holobench_app
        app/main.cpp
        app/Application.cpp
        render/gl/GlDebug.cpp
    )
    set_target_properties(holobench_app PROPERTIES OUTPUT_NAME HoloBench)
    target_include_directories(holobench_app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(holobench_app PRIVATE
        HoloBench::Core
        holobench_imgui
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
        tests/project/ProjectDocumentTests.cpp
    )
    target_link_libraries(holobench_tests PRIVATE HoloBench::Core doctest::doctest)
    holobench_set_warnings(holobench_tests)

    include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)
    doctest_discover_tests(holobench_tests)
endif()
