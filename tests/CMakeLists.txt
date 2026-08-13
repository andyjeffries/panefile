# Test suite (§14). Every module's tests are added in the same commit as the
# module itself.
#
# GUI tests run under the offscreen platform plugin so that the whole suite is
# runnable headless, in CI and over SSH. That is set here rather than left to
# the caller so a bare `ctest` behaves the same as `ctest --preset dev`.

# pf_add_test(<name> SOURCES ... [LIBS ...])
function(pf_add_test name)
    cmake_parse_arguments(ARG "" "" "SOURCES;LIBS" ${ARGN})

    qt_add_executable(${name} ${ARG_SOURCES})
    target_link_libraries(${name} PRIVATE Qt6::Test pf::common_flags ${ARG_LIBS})
    target_include_directories(${name} PRIVATE "${PROJECT_SOURCE_DIR}/src")
    set_target_properties(${name} PROPERTIES MACOSX_BUNDLE OFF)

    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_LOGGING_RULES=panefile.*.debug=false"
        TIMEOUT 120)
endfunction()

pf_add_test(tst_instance    SOURCES tst_instance.cpp    LIBS pf_app)
pf_add_test(tst_commandline SOURCES tst_commandline.cpp LIBS pf_app)
pf_add_test(tst_format      SOURCES tst_format.cpp      LIBS pf_core)
pf_add_test(tst_naturalcompare SOURCES tst_naturalcompare.cpp LIBS pf_core)
pf_add_test(tst_scanner     SOURCES tst_scanner.cpp     LIBS pf_fs pf_model)
pf_add_test(tst_jobs        SOURCES tst_jobs.cpp        LIBS pf_fs)
pf_add_test(tst_watcher     SOURCES tst_watcher.cpp     LIBS pf_fs)
pf_add_test(tst_sorting     SOURCES tst_sorting.cpp     LIBS pf_model)
pf_add_test(tst_chord       SOURCES tst_chord.cpp       LIBS pf_input)
pf_add_test(tst_keymap      SOURCES tst_keymap.cpp      LIBS pf_input pf_app)
pf_add_test(tst_cursormemory SOURCES tst_cursormemory.cpp LIBS pf_ui)
pf_add_test(tst_appearance  SOURCES tst_appearance.cpp  LIBS pf_ui)
# The bundled themes are read from the source tree: the test asserts a property
# of the palettes as shipped, not of whatever happens to be installed.
target_compile_definitions(tst_appearance PRIVATE
    PF_THEMES_DIR="${PROJECT_SOURCE_DIR}/data/themes")
pf_add_test(tst_dragdrop    SOURCES tst_dragdrop.cpp    LIBS pf_ui)
pf_add_test(tst_filepanel   SOURCES tst_filepanel.cpp   LIBS pf_ui pf_input)
pf_add_test(tst_panels      SOURCES tst_panels.cpp      LIBS pf_ui pf_app)
pf_add_test(tst_fuzzymatcher SOURCES tst_fuzzymatcher.cpp LIBS pf_core)
pf_add_test(tst_search      SOURCES tst_search.cpp      LIBS pf_fs pf_model)
pf_add_test(tst_rename      SOURCES tst_rename.cpp      LIBS pf_fs)
pf_add_test(tst_quicklook  SOURCES tst_quicklook.cpp  LIBS pf_ui)
pf_add_test(tst_thumbnails SOURCES tst_thumbnails.cpp LIBS pf_model)
pf_add_test(tst_archives    SOURCES tst_archives.cpp    LIBS pf_fs)
pf_add_test(tst_mounts      SOURCES tst_mounts.cpp      LIBS pf_platform)
pf_add_test(tst_paths       SOURCES tst_paths.cpp       LIBS pf_platform)
pf_add_test(tst_startuptrace SOURCES tst_startuptrace.cpp LIBS pf_core)
# toml++ is a private dependency of pf_config — DefaultConfig.h does not expose
# it — so this test, which reads the parsed table directly, links it itself.
pf_add_test(tst_defaultconfig SOURCES tst_defaultconfig.cpp LIBS pf_config pf::tomlplusplus)
pf_add_test(tst_config      SOURCES tst_config.cpp      LIBS pf_config pf_input)
