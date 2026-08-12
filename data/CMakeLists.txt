set(PF_DESKTOP_EXEC "pf")

# Stage the read-only data next to the binaries, so a build tree behaves like
# an install: `./build/dev/bin/pf` finds the bundled themes, and so do the
# tests, without anyone having to set PANEFILE_DATA_DIR first.
add_custom_target(pf_stage_data ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/themes" "${CMAKE_BINARY_DIR}/data/themes"
    COMMENT "Staging themes into the build tree")

configure_file(panefile.desktop.in "${CMAKE_CURRENT_BINARY_DIR}/panefile.desktop" @ONLY)
configure_file(pf.1.in "${CMAKE_CURRENT_BINARY_DIR}/pf.1" @ONLY)

# Validating the desktop entry at build time keeps a malformed MimeType or Exec
# line from shipping: §10.5 depends on this file being correct for Panefile to
# be selectable as the default file manager.
find_program(PF_DESKTOP_FILE_VALIDATE desktop-file-validate)
if(PF_DESKTOP_FILE_VALIDATE AND PF_PLATFORM_LINUX)
    add_custom_target(validate-desktop-file ALL
        COMMAND "${PF_DESKTOP_FILE_VALIDATE}" "${CMAKE_CURRENT_BINARY_DIR}/panefile.desktop"
        DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/panefile.desktop"
        COMMENT "Validating panefile.desktop")
endif()
