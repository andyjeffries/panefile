# Shared compile and link settings, exposed as the interface target
# pf::common_flags which every Panefile target links.
#
# The linker settings here are load-time settings, not micro-optimisations:
# §3.4 makes cold start an acceptance criterion, and the two most effective
# levers are "don't record dependencies you don't call" and "bind lazily".

include(CheckIPOSupported)

add_library(pf_common_flags INTERFACE)
add_library(pf::common_flags ALIAS pf_common_flags)

target_compile_features(pf_common_flags INTERFACE cxx_std_20)

target_compile_definitions(pf_common_flags INTERFACE
    QT_NO_CAST_FROM_BYTEARRAY
    QT_NO_FOREACH
    QT_USE_QSTRINGBUILDER
    QT_DISABLE_DEPRECATED_UP_TO=0x060700   # §16: no Qt5 compatibility shims
    $<$<NOT:$<CONFIG:Debug>>:QT_NO_DEBUG_OUTPUT>)

if(PF_PLATFORM_LINUX)
    target_compile_definitions(pf_common_flags INTERFACE PF_PLATFORM_LINUX=1)
elseif(PF_PLATFORM_DARWIN)
    target_compile_definitions(pf_common_flags INTERFACE PF_PLATFORM_DARWIN=1)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(pf_common_flags INTERFACE
        -Wall -Wextra -Wpedantic
        -Wcast-qual -Wnon-virtual-dtor -Woverloaded-virtual
        -Wno-missing-field-initializers)

    if(PF_WARNINGS_AS_ERRORS)
        target_compile_options(pf_common_flags INTERFACE -Werror)
    endif()

    target_compile_options(pf_common_flags INTERFACE
        $<$<CONFIG:Release,RelWithDebInfo>:-O2>
        $<$<CONFIG:Debug>:-O0 -g3>)
endif()

if(PF_PLATFORM_LINUX AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    # --as-needed: drop DT_NEEDED entries for libraries we never call into.
    # -z lazy: lazy PLT binding, deliberately *countering* the -z now that both
    # Arch's makepkg and Ubuntu's dpkg-buildflags inject by default. Eagerly
    # resolving every Qt relocation at every launch is exactly the cost §3.4
    # tells us to avoid.
    target_link_options(pf_common_flags INTERFACE
        LINKER:--as-needed
        LINKER:-z,lazy)
endif()

if(PF_ENABLE_ASAN)
    set(PF_SANITIZERS -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined)
    target_compile_options(pf_common_flags INTERFACE ${PF_SANITIZERS})
    target_link_options(pf_common_flags INTERFACE ${PF_SANITIZERS})
endif()

# Link-time optimisation for optimised builds only; it costs too much link time
# to be worth it during development.
check_ipo_supported(RESULT PF_IPO_SUPPORTED OUTPUT PF_IPO_ERROR)
if(PF_IPO_SUPPORTED AND NOT PF_ENABLE_ASAN)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO ON)
endif()

# pf_layer_library(<name> LAYER <layer> SOURCES ... [DEPENDS ...])
#
# Declares one of the architectural layers of §3.1. Dependencies point downward
# only; pf_check_layering() below fails the configure if that is violated.
#
# Two refinements to the diagram in §3.1, both of which it implies rather than
# states:
#
#   * `core` sits underneath everything — logging categories and the startup
#     trace are needed by every layer including fs, and hanging them off
#     `platform` would misfile them.
#
#   * `app` sits *above* `ui` rather than below it. §3.1's hard requirement is
#     the one it spells out — "fs/ must not include anything from ui/" — and
#     that is about keeping presentation out of the lower layers. But `app`
#     holds the composition root: Application owns the window, PanelController
#     creates panels. That is an inherently upward dependency, and inverting it
#     with interfaces would buy nothing. The dispatch machinery the UI must call
#     into — ActionRegistry and Keymap, which are pure lookup with no widget
#     dependencies of their own — lives in `input` below `ui` instead, so §6.2's
#     "nothing in the UI may call a behaviour function directly" still holds
#     without a cycle.
set(PF_LAYER_ORDER core platform config fs model input ui app)

function(pf_layer_library name)
    cmake_parse_arguments(ARG "" "LAYER" "SOURCES;DEPENDS;PUBLIC_DEPENDS" ${ARGN})

    string(REGEX REPLACE "^pf_" "" alias_name "${name}")
    add_library(${name} STATIC ${ARG_SOURCES})
    add_library(pf::${alias_name} ALIAS ${name})
    set_property(TARGET ${name} PROPERTY PF_LAYER "${ARG_LAYER}")
    set_property(TARGET ${name} PROPERTY POSITION_INDEPENDENT_CODE ON)

    target_include_directories(${name} PUBLIC "${PROJECT_SOURCE_DIR}/src")
    target_link_libraries(${name} PUBLIC pf::common_flags ${ARG_PUBLIC_DEPENDS})
    if(ARG_DEPENDS)
        target_link_libraries(${name} PRIVATE ${ARG_DEPENDS})
    endif()
endfunction()

# Verifies §3.1: "Dependencies point downward only. fs/ must not include
# anything from ui/." Run at the end of configuration over every declared layer
# target, so a bad link line is a configure error rather than a code review note.
function(pf_check_layering)
    foreach(target IN LISTS ARGN)
        if(NOT TARGET ${target})
            continue()
        endif()
        get_target_property(layer ${target} PF_LAYER)
        list(FIND PF_LAYER_ORDER "${layer}" layer_index)

        set(all_deps "")
        get_target_property(link_libs ${target} LINK_LIBRARIES)
        get_target_property(iface_libs ${target} INTERFACE_LINK_LIBRARIES)
        foreach(lib IN LISTS link_libs iface_libs)
            if(TARGET ${lib})
                list(APPEND all_deps ${lib})
            endif()
        endforeach()

        foreach(dep IN LISTS all_deps)
            get_target_property(dep_layer ${dep} PF_LAYER)
            if(NOT dep_layer)
                continue()
            endif()
            list(FIND PF_LAYER_ORDER "${dep_layer}" dep_index)
            if(dep_index GREATER_EQUAL layer_index)
                message(FATAL_ERROR
                    "Layering violation (§3.1): ${target} (layer '${layer}') depends on "
                    "${dep} (layer '${dep_layer}'). Dependencies must point downward only.")
            endif()
        endforeach()
    endforeach()
endfunction()
