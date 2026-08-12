# Build options and platform detection.
#
# Panefile targets Linux (primary) and macOS. Every OS-specific concern lives
# behind an interface in src/platform/ with one implementation per platform,
# selected here rather than with #ifdef inside shared translation units.

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(PF_PLATFORM_LINUX ON)
    set(PF_PLATFORM_ID "linux")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(PF_PLATFORM_DARWIN ON)
    set(PF_PLATFORM_ID "darwin")
else()
    message(FATAL_ERROR
        "Panefile supports Linux and macOS. CMAKE_SYSTEM_NAME is '${CMAKE_SYSTEM_NAME}'.")
endif()

option(PF_BUILD_TESTS "Build the test suite" ON)

# §2: optional features. Each builds a QPluginLoader plugin that the main binary
# loads on first use, so a missing dependency degrades gracefully at runtime and
# a present one never becomes a DT_NEEDED entry on the startup path (§3.4).
option(PF_ENABLE_SYNTAX       "Syntax highlighting in Quick Look (KSyntaxHighlighting)" ON)
option(PF_ENABLE_MEDIA        "Video and audio playback in Quick Look (QtMultimedia)"   ON)
option(PF_ENABLE_PDF          "PDF rendering in Quick Look (QtPdf, poppler-qt6)"        ON)
option(PF_ENABLE_VIDEO_THUMBS "Video thumbnails (libffmpegthumbnailer)"                 ON)

# KSyntaxHighlighting is packaged on Arch as ksyntax-highlighting but is absent
# from Homebrew. This builds it from source (it depends only on Qt and ECM).
option(PF_FETCH_KSYNTAX "Build KSyntaxHighlighting from source if not installed" OFF)

option(PF_ENABLE_ASAN "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(PF_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
