# Prints what was actually enabled. Optional features degrade silently at
# runtime by design (§3.4), so the configure summary is the only place a build
# tells you which of them you are getting.

message(STATUS "")
message(STATUS "  Panefile ${PROJECT_VERSION}")
message(STATUS "  ------------------------------------------------------------")
message(STATUS "  Platform             ${CMAKE_SYSTEM_NAME} (${PF_PLATFORM_ID})")
message(STATUS "  Compiler             ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  Build type           ${CMAKE_BUILD_TYPE}")
message(STATUS "  Qt                   ${Qt6_VERSION}")
message(STATUS "  Tests                ${PF_BUILD_TESTS}")
message(STATUS "  Sanitizers           ${PF_ENABLE_ASAN}")
message(STATUS "  ------------------------------------------------------------")

foreach(feature IN ITEMS SYNTAX MEDIA PDF VIDEO_THUMBS)
    if(NOT PF_ENABLE_${feature})
        set(state "off (disabled)")
    elseif(PF_HAVE_${feature})
        set(state "on")
    else()
        set(state "off (dependency not found)")
    endif()
    message(STATUS "  ${feature}${PF_PAD_${feature}}  ${state}")
endforeach()

message(STATUS "  ------------------------------------------------------------")
message(STATUS "")
