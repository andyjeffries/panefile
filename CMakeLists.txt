cmake_minimum_required(VERSION 3.25)

project(panefile
    VERSION 0.1.0
    DESCRIPTION "Keyboard-driven, multi-panel file manager"
    HOMEPAGE_URL "https://github.com/andyjeffries/panefile"
    LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(PanefileOptions)
include(PanefileCompilerFlags)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC OFF)   # no .ui files; every widget is constructed in code

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

# §2: Core, Gui, Widgets, DBus, Concurrent, Test are required. DBus is only
# actually *used* on Linux (udisks2, §7.11) but the module is cheap to find and
# keeping it unconditional keeps the find_package call identical everywhere.
set(PF_REQUIRED_QT_COMPONENTS Core Gui Widgets Concurrent)
if(PF_PLATFORM_LINUX)
    list(APPEND PF_REQUIRED_QT_COMPONENTS DBus)
endif()

find_package(Qt6 6.7 REQUIRED COMPONENTS ${PF_REQUIRED_QT_COMPONENTS})
qt_standard_project_setup()

if(PF_BUILD_TESTS)
    find_package(Qt6 6.7 REQUIRED COMPONENTS Test)
    enable_testing()
endif()

add_subdirectory(third_party)
add_subdirectory(src)

if(PF_BUILD_TESTS)
    add_subdirectory(tests)
endif()

add_subdirectory(data)

include(PanefileInstall)
include(PanefileSummary)
