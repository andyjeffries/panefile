# The optional-feature plugins (§2, §3.4).
#
# "Optional heavy dependencies (KSyntaxHighlighting, QtMultimedia, QtPdf,
# poppler, libffmpegthumbnailer) must not be direct link-time dependencies of
# the main binary. Build each as a small plugin .so loaded with QPluginLoader on
# first use." platform/PluginHost.cpp does the loading; this builds them.
#
# Each is looked for quietly and skipped when its dependency is missing. That
# is §2's graceful degradation at build time: the sanitizer and lint jobs, the
# Ubuntu job and Homebrew all lack one or more of these, and a build without a
# plugin is a supported build, not a broken one. PanefileSummary.cmake reports
# which were built.
#
# Plugins link Qt and their own dependency, and nothing from pf's layers — see
# plugins/PluginInterfaces.h for why.

# Staged where an install puts them relative to the binary (bin/../lib/...), so
# that a build tree finds its plugins exactly as an installed copy does.
set(PF_PLUGIN_OUTPUT_DIR "${CMAKE_BINARY_DIR}/lib/panefile/plugins")

foreach(feature IN ITEMS SYNTAX MEDIA PDF VIDEO_THUMBS)
    set(PF_HAVE_${feature} OFF CACHE INTERNAL "")
    set(PF_DETAIL_${feature} "" CACHE INTERNAL "")
endforeach()
set_property(GLOBAL PROPERTY PF_PLUGIN_TARGETS "")

# pf_add_plugin(<name> FEATURE <feature> SOURCES ... LIBS ...)
function(pf_add_plugin name)
    cmake_parse_arguments(ARG "" "FEATURE" "SOURCES;LIBS" ${ARGN})

    qt_add_plugin(${name} SHARED ${ARG_SOURCES})
    target_include_directories(${name} PRIVATE "${PROJECT_SOURCE_DIR}/src")
    target_link_libraries(${name} PRIVATE pf::common_flags Qt6::Core ${ARG_LIBS})

    # No lib prefix: the host looks for pf-syntax.so, which is also the name
    # the file has in a package listing.
    set_target_properties(${name} PROPERTIES
        PREFIX ""
        LIBRARY_OUTPUT_DIRECTORY "${PF_PLUGIN_OUTPUT_DIR}")

    # Built with the binary, never linked by it.
    add_dependencies(pf ${name})

    if(PF_PLATFORM_DARWIN)
        # A bundle's executable is three directories below the build tree's
        # lib/, so the build tree's bundle gets its own copy, where an
        # installed bundle keeps them (PanefileInstall.cmake).
        add_custom_command(TARGET ${name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_BUNDLE_CONTENT_DIR:pf>/PlugIns/panefile"
            COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:${name}>"
                "$<TARGET_BUNDLE_CONTENT_DIR:pf>/PlugIns/panefile/"
            COMMENT "Staging ${name} into the bundle")
    endif()

    set_property(GLOBAL APPEND PROPERTY PF_PLUGIN_TARGETS ${name})
    set(PF_HAVE_${ARG_FEATURE} ON CACHE INTERNAL "")
endfunction()

find_package(PkgConfig QUIET)

# pf-syntax — KSyntaxHighlighting, packaged on Arch as syntax-highlighting.
if(PF_ENABLE_SYNTAX)
    find_package(KF6SyntaxHighlighting QUIET)
    if(KF6SyntaxHighlighting_FOUND)
        pf_add_plugin(pf-syntax FEATURE SYNTAX
            SOURCES syntax/SyntaxPlugin.cpp
            LIBS Qt6::Gui KF6::SyntaxHighlighting)
    endif()
endif()

# pf-media — QtMultimedia, with its widgets module for QVideoWidget.
if(PF_ENABLE_MEDIA)
    find_package(Qt6 QUIET COMPONENTS Multimedia MultimediaWidgets)
    if(Qt6Multimedia_FOUND AND Qt6MultimediaWidgets_FOUND)
        pf_add_plugin(pf-media FEATURE MEDIA
            SOURCES media/MediaPlugin.cpp
            LIBS Qt6::Widgets Qt6::Multimedia Qt6::MultimediaWidgets)
    endif()
endif()

# pf-pdf — poppler-qt6 first, QtPdf as the fallback; see the comment at the top
# of pdf/PdfPlugin.cpp for why that reverses §7.6's stated preference.
if(PF_ENABLE_PDF)
    if(PkgConfig_FOUND)
        pkg_check_modules(PF_POPPLER_QT6 QUIET IMPORTED_TARGET poppler-qt6)
    endif()
    if(PF_POPPLER_QT6_FOUND)
        pf_add_plugin(pf-pdf FEATURE PDF
            SOURCES pdf/PdfPlugin.cpp
            LIBS Qt6::Widgets Qt6::Concurrent PkgConfig::PF_POPPLER_QT6)
        target_compile_definitions(pf-pdf PRIVATE PF_PDF_POPPLER=1)
        set(PF_DETAIL_PDF "poppler-qt6" CACHE INTERNAL "")
    else()
        find_package(Qt6 QUIET COMPONENTS Pdf)
        if(Qt6Pdf_FOUND)
            pf_add_plugin(pf-pdf FEATURE PDF
                SOURCES pdf/PdfPlugin.cpp
                LIBS Qt6::Widgets Qt6::Concurrent Qt6::Pdf)
            target_compile_definitions(pf-pdf PRIVATE PF_PDF_QTPDF=1)
            set(PF_DETAIL_PDF "QtPdf" CACHE INTERNAL "")
        endif()
    endif()
endif()

# pf-video-thumb — libffmpegthumbnailer, which ships a pkg-config file and no
# CMake package.
if(PF_ENABLE_VIDEO_THUMBS)
    if(PkgConfig_FOUND)
        pkg_check_modules(PF_FFMPEGTHUMBNAILER QUIET IMPORTED_TARGET libffmpegthumbnailer)
    endif()
    if(PF_FFMPEGTHUMBNAILER_FOUND)
        pf_add_plugin(pf-video-thumb FEATURE VIDEO_THUMBS
            SOURCES videothumb/VideoThumbnailPlugin.cpp
            LIBS Qt6::Gui PkgConfig::PF_FFMPEGTHUMBNAILER)
    endif()
endif()
