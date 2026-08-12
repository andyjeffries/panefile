string(TOLOWER "${PROJECT_NAME}" PF_APPLICATION_NAME_LC)
include(GNUInstallDirs)

configure_file(core/Version.h.in "${CMAKE_CURRENT_BINARY_DIR}/core/Version.h" @ONLY)

# ---------------------------------------------------------------------------
# core — logging, startup instrumentation, generated version header.
# The bottom of the stack: everything may depend on it, it depends on nothing.
# ---------------------------------------------------------------------------
pf_layer_library(pf_core
    LAYER core
    SOURCES
        core/Logging.cpp
        core/StartupTrace.cpp
    PUBLIC_DEPENDS Qt6::Core)

target_include_directories(pf_core PUBLIC "${CMAKE_CURRENT_BINARY_DIR}")

# ---------------------------------------------------------------------------
# platform — the OS seam (§3.1). One implementation directory per platform,
# chosen here so that shared translation units stay free of #ifdef.
# ---------------------------------------------------------------------------
set(PF_PLATFORM_SOURCES
    platform/Paths.cpp)

if(PF_PLATFORM_LINUX)
    list(APPEND PF_PLATFORM_SOURCES platform/linux/Paths_linux.cpp)
elseif(PF_PLATFORM_DARWIN)
    list(APPEND PF_PLATFORM_SOURCES platform/darwin/Paths_darwin.cpp)
endif()

pf_layer_library(pf_platform
    LAYER platform
    SOURCES ${PF_PLATFORM_SOURCES}
    PUBLIC_DEPENDS Qt6::Core pf_core)

# ---------------------------------------------------------------------------
# config — TOML loading, validation and theming.
# ---------------------------------------------------------------------------
pf_layer_library(pf_config
    LAYER config
    SOURCES
        config/DefaultConfig.cpp
    PUBLIC_DEPENDS Qt6::Core pf_core pf_platform
    DEPENDS pf::tomlplusplus)

# ---------------------------------------------------------------------------
# ui — widgets, delegates, painting.
# ---------------------------------------------------------------------------
pf_layer_library(pf_ui
    LAYER ui
    SOURCES
        ui/MainWindow.cpp
    PUBLIC_DEPENDS Qt6::Core Qt6::Gui Qt6::Widgets pf_core pf_platform pf_config)

# ---------------------------------------------------------------------------
# app — the composition root: lifecycle, session, panel controller.
# ---------------------------------------------------------------------------
pf_layer_library(pf_app
    LAYER app
    SOURCES
        app/Application.cpp
        app/CommandLine.cpp
    PUBLIC_DEPENDS Qt6::Core Qt6::Gui Qt6::Widgets pf_core pf_platform pf_config)

# Public rather than private: main.cpp is handed the window through Application.
target_link_libraries(pf_app PUBLIC pf_ui)

pf_check_layering(pf_core pf_platform pf_config pf_ui pf_app)

# ---------------------------------------------------------------------------
# pf — the binary.
# ---------------------------------------------------------------------------
qt_add_executable(pf MACOSX_BUNDLE main.cpp)

target_link_libraries(pf PRIVATE pf_app)

set_target_properties(pf PROPERTIES
    OUTPUT_NAME pf
    WIN32_EXECUTABLE OFF)

if(PF_PLATFORM_DARWIN)
    set_target_properties(pf PROPERTIES
        MACOSX_BUNDLE_BUNDLE_NAME "Panefile"
        MACOSX_BUNDLE_GUI_IDENTIFIER "org.panefile.Panefile"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_INFO_PLIST "${PROJECT_SOURCE_DIR}/packaging/macos/Info.plist.in")
endif()
