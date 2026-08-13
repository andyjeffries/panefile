# Development tools. Not built by default and never installed: they exist to
# produce artefacts that are committed, so a normal build has no reason to pay
# for them.

qt_add_executable(pf-screenshot screenshot.cpp)
target_link_libraries(pf-screenshot PRIVATE Qt6::Widgets pf_ui pf_config)
target_include_directories(pf-screenshot PRIVATE "${PROJECT_SOURCE_DIR}/src")
set_target_properties(pf-screenshot PROPERTIES MACOSX_BUNDLE OFF)


