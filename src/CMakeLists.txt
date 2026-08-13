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
        core/Format.cpp
        core/Logging.cpp
        core/FuzzyMatcher.cpp
        core/NaturalCompare.cpp
        core/StartupTrace.cpp
    PUBLIC_DEPENDS Qt6::Core)

target_include_directories(pf_core PUBLIC "${CMAKE_CURRENT_BINARY_DIR}")

# ---------------------------------------------------------------------------
# platform — the OS seam (§3.1). One implementation directory per platform,
# chosen here so that shared translation units stay free of #ifdef.
# ---------------------------------------------------------------------------
set(PF_PLATFORM_SOURCES
    platform/FileOpsCommon.cpp
    platform/Paths.cpp
    platform/WatchBackend.cpp)

if(PF_PLATFORM_LINUX)
    list(APPEND PF_PLATFORM_SOURCES
        platform/linux/FileOps_linux.cpp
        platform/linux/Paths_linux.cpp
        platform/linux/WatchBackend_linux.cpp)
elseif(PF_PLATFORM_DARWIN)
    list(APPEND PF_PLATFORM_SOURCES
        platform/darwin/FileOps_darwin.cpp
        platform/darwin/Paths_darwin.cpp
        platform/darwin/WatchBackend_darwin.cpp)
endif()

pf_layer_library(pf_platform
    LAYER platform
    SOURCES ${PF_PLATFORM_SOURCES}
    PUBLIC_DEPENDS Qt6::Core pf_core)

# ---------------------------------------------------------------------------
# input — chords, keymap and the action registry.
#
# Below ui rather than above it (§3.1's note in PanefileCompilerFlags.cmake):
# these are pure lookup with no widget dependencies, which is what lets §6.2's
# "nothing in the UI may call a behaviour function directly" hold without a
# cycle between ui and app.
# ---------------------------------------------------------------------------
pf_layer_library(pf_input
    LAYER input
    SOURCES
        input/ActionRegistry.cpp
        input/Chord.cpp
        input/DefaultKeymap.cpp
        input/Keymap.cpp
    PUBLIC_DEPENDS Qt6::Core Qt6::Gui pf_core)

# ---------------------------------------------------------------------------
# config — TOML loading, validation and theming.
# ---------------------------------------------------------------------------
pf_layer_library(pf_config
    LAYER config
    SOURCES
        config/Config.cpp
        config/ConfigWatcher.cpp
        config/DefaultConfig.cpp
        config/Hotkeys.cpp
        config/StyleSheetBuilder.cpp
        config/Theme.cpp
    PUBLIC_DEPENDS Qt6::Core Qt6::Gui pf_core pf_platform pf_input
    DEPENDS pf::tomlplusplus)

# ---------------------------------------------------------------------------
# fs — scanning, watching, jobs, trash, archives, mounts.
# ---------------------------------------------------------------------------
pf_layer_library(pf_fs
    LAYER fs
    SOURCES
        fs/ArchiveReader.cpp
        fs/DirectoryScanner.cpp
        fs/DirectoryWatcher.cpp
        fs/FsError.cpp
        fs/WatchCoalescer.cpp
        fs/Job.cpp
        fs/JobEngine.cpp
        fs/LibArchive.cpp
        fs/RecursiveFinder.cpp
        fs/RenamePlan.cpp
        fs/RenameRule.cpp
        fs/Trash.cpp
        fs/UndoStack.cpp
        fs/jobs/DeleteJob.cpp
        fs/jobs/RenameJob.cpp
        fs/jobs/TransferJob.cpp
    PUBLIC_DEPENDS Qt6::Core pf_core pf_platform)

if(PF_HAVE_LIBARCHIVE)
    # Headers only, deliberately. §3.4: "Optional heavy dependencies … must not
    # be direct link-time dependencies of the main binary. … A DT_NEEDED entry
    # costs relocation and page-in time at every launch whether or not the code
    # is called." fs/LibArchive.cpp resolves the symbols with QLibrary on the
    # first archive the user opens, so the include directories are wanted and
    # the link is not.
    target_compile_definitions(pf_fs PRIVATE PF_HAVE_LIBARCHIVE=1)
    target_include_directories(pf_fs PRIVATE ${LibArchive_INCLUDE_DIRS})
endif()

# ---------------------------------------------------------------------------
# model — the directory model, its proxy, and icon resolution.
# ---------------------------------------------------------------------------
pf_layer_library(pf_model
    LAYER model
    SOURCES
        model/DirectoryModel.cpp
        model/FilterSortProxy.cpp
        model/IconProvider.cpp
        model/ThumbnailCache.cpp
    PUBLIC_DEPENDS Qt6::Core Qt6::Gui pf_core pf_platform pf_fs)

# §4.3's bundled fallback icon set, embedded rather than installed: it is the
# primary icon set on macOS, and on Linux a listing still has to paint when the
# desktop's icon theme is missing an entry. §3.4 allows exactly this — "icons
# that are actually needed at startup" — and nothing else.
qt_add_resources(pf_model "fallback_icons"
    PREFIX "/icons/fallback"
    BASE "${PROJECT_SOURCE_DIR}/data/icons/fallback"
    FILES
        "${PROJECT_SOURCE_DIR}/data/icons/fallback/archive.svg"
        "${PROJECT_SOURCE_DIR}/data/icons/fallback/code.svg"
        "${PROJECT_SOURCE_DIR}/data/icons/fallback/executable.svg"
        "${PROJECT_SOURCE_DIR}/data/icons/fallback/file.svg"
        "${PROJECT_SOURCE_DIR}/data/icons/fallback/folder.svg"
        "${PROJECT_SOURCE_DIR}/data/icons/fallback/image.svg"
        "${PROJECT_SOURCE_DIR}/data/icons/fallback/symlink.svg"
        "${PROJECT_SOURCE_DIR}/data/icons/fallback/text.svg")

# ---------------------------------------------------------------------------
# ui — widgets, delegates, painting.
# ---------------------------------------------------------------------------
pf_layer_library(pf_ui
    LAYER ui
    SOURCES
        ui/CursorMemory.cpp
        ui/FileItemDelegate.cpp
        ui/FilePanel.cpp
        ui/MainWindow.cpp
        ui/PanelStrip.cpp
        ui/ProcessBar.cpp
        ui/Sidebar.cpp
        ui/ThemePalette.cpp
        ui/modals/ConflictModal.cpp
        ui/quicklook/QuickLookDock.cpp
        ui/quicklook/QuickLookLoader.cpp
        ui/quicklook/QuickLookOverlay.cpp
        ui/quicklook/QuickLookRegistry.cpp
        ui/quicklook/QuickLookView.cpp
        ui/quicklook/renderers/ArchiveRenderer.cpp
        ui/quicklook/renderers/DirectoryRenderer.cpp
        ui/quicklook/renderers/HexRenderer.cpp
        ui/quicklook/renderers/ImageRenderer.cpp
        ui/quicklook/renderers/MediaRenderer.cpp
        ui/quicklook/renderers/PdfRenderer.cpp
        ui/quicklook/renderers/TextRenderer.cpp
        ui/modals/FindModal.cpp
        ui/modals/InputModal.cpp
        ui/modals/HelpModal.cpp
        ui/modals/RenameModal.cpp
        ui/modals/Modal.cpp
    PUBLIC_DEPENDS
        Qt6::Core Qt6::Gui Qt6::Widgets
        pf_core pf_platform pf_config pf_model pf_input pf_fs)

# ---------------------------------------------------------------------------
# app — the composition root: lifecycle, session, panel controller.
# ---------------------------------------------------------------------------
pf_layer_library(pf_app
    LAYER app
    SOURCES
        app/Application.cpp
        app/CommandLine.cpp
        app/FileOperations.cpp
        app/KeyDispatcher.cpp
        app/PanelController.cpp
        app/QuickLookController.cpp
        app/SearchController.cpp
    PUBLIC_DEPENDS
        Qt6::Core Qt6::Gui Qt6::Widgets
        pf_core pf_platform pf_config pf_input pf_fs)

# Public rather than private: main.cpp is handed the window through Application.
target_link_libraries(pf_app PUBLIC pf_ui)

pf_check_layering(pf_core pf_platform pf_config pf_fs pf_model pf_input pf_ui pf_app)

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
