cmake_minimum_required(VERSION 3.22)
project(MX300Control VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES arm64)
    set(CMAKE_OSX_DEPLOYMENT_TARGET 12.0)
endif()

option(MX300_BUILD_PLUGIN "Build the JUCE app and plugins" ON)
set(MX300_JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/JUCE" CACHE PATH "Local JUCE checkout (downloaded if missing)")

# ---- core: protocol, parameter model, device session (no JUCE)
add_library(mx300core STATIC
    core/Protocol.cpp
    core/ParamModel.cpp
    core/ParamData.cpp
    core/DeviceSession.cpp
    core/FakeUnit.cpp)
target_include_directories(mx300core PUBLIC core)
set_target_properties(mx300core PROPERTIES POSITION_INDEPENDENT_CODE ON)   # linked into the plugin shared libraries
if(MSVC)
    target_compile_options(mx300core PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(mx300core PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
endif()

enable_testing()
add_executable(mx300core_tests core/tests/CoreTests.cpp)
target_link_libraries(mx300core_tests PRIVATE mx300core)
add_test(NAME core COMMAND mx300core_tests)

if(MX300_BUILD_PLUGIN)
    if(EXISTS "${MX300_JUCE_DIR}/CMakeLists.txt")
        add_subdirectory("${MX300_JUCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE")
    else()
        include(FetchContent)
        FetchContent_Declare(JUCE
            GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
            GIT_TAG 9.0.4
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(JUCE)
    endif()
    add_subdirectory(plugin)
endif()
