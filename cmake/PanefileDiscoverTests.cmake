# Run with cmake -P after a test binary links: asks the binary for its test
# functions and writes one ctest test per function into OUTPUT.
#
# QtTest's own listing (-functions) is the source of truth, rather than parsing
# the slots out of the source, so what ctest runs is exactly what the binary
# would have run. It is the same shape as gtest_discover_tests.
#
# PARAMS is a file setting `name`, `command` (any wrapper, then the binary) and
# `environment`, written by pf_add_test at generate time. The listing runs
# under the same wrapper and environment as the tests themselves, so it is
# headless and sandboxed in exactly the same way.

include("${PARAMS}")

foreach(assignment IN LISTS environment)
    if(assignment MATCHES "^([^=]+)=(.*)$")
        set("ENV{${CMAKE_MATCH_1}}" "${CMAKE_MATCH_2}")
    endif()
endforeach()

execute_process(
    COMMAND ${command} -functions
    OUTPUT_VARIABLE listing
    ERROR_VARIABLE errors
    RESULT_VARIABLE result
    TIMEOUT 60)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "${name} -functions failed (${result}):\n${listing}${errors}")
endif()

set(quoted_command "")
foreach(argument IN LISTS command)
    string(APPEND quoted_command " [==[${argument}]==]")
endforeach()

set(script "")
set(count 0)
string(REPLACE "\n" ";" lines "${listing}")
foreach(line IN LISTS lines)
    if(NOT line MATCHES "^([A-Za-z_][A-Za-z0-9_]*)\\(\\)$")
        continue()
    endif()
    set(function "${CMAKE_MATCH_1}")
    set(test "${name}::${function}")
    string(APPEND script
        "add_test([==[${test}]==]${quoted_command} [==[${function}]==])\n"
        "set_tests_properties([==[${test}]==] PROPERTIES\n"
        "    ENVIRONMENT [==[${environment}]==] TIMEOUT 120 LABELS [==[${name}]==])\n")
    math(EXPR count "${count} + 1")
endforeach()

# A binary that lists nothing would otherwise vanish from the suite silently.
if(count EQUAL 0)
    message(FATAL_ERROR "${name} -functions listed no test functions:\n${listing}${errors}")
endif()

file(WRITE "${OUTPUT}" "${script}")
