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

pf_add_test(tst_commandline SOURCES tst_commandline.cpp LIBS pf_app)
pf_add_test(tst_format      SOURCES tst_format.cpp      LIBS pf_core)
pf_add_test(tst_naturalcompare SOURCES tst_naturalcompare.cpp LIBS pf_core)
pf_add_test(tst_scanner     SOURCES tst_scanner.cpp     LIBS pf_fs pf_model)
pf_add_test(tst_sorting     SOURCES tst_sorting.cpp     LIBS pf_model)
pf_add_test(tst_cursormemory SOURCES tst_cursormemory.cpp LIBS pf_ui)
pf_add_test(tst_filepanel   SOURCES tst_filepanel.cpp   LIBS pf_ui)
pf_add_test(tst_paths       SOURCES tst_paths.cpp       LIBS pf_platform)
pf_add_test(tst_startuptrace SOURCES tst_startuptrace.cpp LIBS pf_core)
# toml++ is a private dependency of pf_config — DefaultConfig.h does not expose
# it — so this test, which reads the parsed table directly, links it itself.
pf_add_test(tst_defaultconfig SOURCES tst_defaultconfig.cpp LIBS pf_config pf::tomlplusplus)
