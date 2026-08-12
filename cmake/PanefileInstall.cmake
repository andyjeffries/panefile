# Install rules (§15).
#
# Linux gets a conventional prefix install so that distribution packaging is a
# thin wrapper. macOS gets an application bundle instead, since a .app is what
# Launch Services needs in order to offer Panefile as a folder handler.

include(GNUInstallDirs)

if(PF_PLATFORM_DARWIN)
    install(TARGETS pf
        BUNDLE  DESTINATION .
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

    install(DIRECTORY "${PROJECT_SOURCE_DIR}/data/themes"
        DESTINATION "Panefile.app/Contents/Resources")
else()
    install(TARGETS pf RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

    install(DIRECTORY "${PROJECT_SOURCE_DIR}/data/themes"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/panefile")

    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/data/panefile.desktop"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")

    install(FILES "${PROJECT_SOURCE_DIR}/data/icons/panefile.svg"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps")

    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/data/pf.1"
        DESTINATION "${CMAKE_INSTALL_MANDIR}/man1")
endif()
