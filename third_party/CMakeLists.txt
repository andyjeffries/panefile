# Vendored dependencies.
#
# toml++ is vendored as its single-header amalgamation rather than pulled with
# FetchContent so that a fresh configure never needs the network.

add_library(pf_tomlplusplus INTERFACE)
add_library(pf::tomlplusplus ALIAS pf_tomlplusplus)

target_include_directories(pf_tomlplusplus SYSTEM INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}")

target_compile_definitions(pf_tomlplusplus INTERFACE
    TOML_EXCEPTIONS=0        # config errors are reported, never thrown (§8.3)
    TOML_ENABLE_FORMATTERS=0)
