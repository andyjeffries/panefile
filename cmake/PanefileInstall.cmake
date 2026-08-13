# Install rules (§15).
#
# Linux gets a conventional prefix install so that distribution packaging is a
# thin wrapper. macOS gets an application bundle instead, since a .app is what
# Launch Services needs in order to offer Panefile as a folder handler.

include(GNUInstallDirs)

if(PF_PLATFORM_DARWIN)
    install(TARGETS pf BUNDLE DESTINATION .)

    # `pf` on the PATH is a wrapper that execs the bundle's executable, not a
    # second copy of it and not a symlink to it.
    #
    # macOS works out what an application is from where its executable lives:
    # CFBundleGetMainBundle walks up from the running executable's path looking
    # for Contents/MacOS. A loose copy in bin/ has no bundle above it, and a
    # symlink is worse still — the kernel reports the path the process was
    # launched by rather than the one it resolves to, so the bundle is missed
    # even though the real binary is sitting inside it.
    #
    # Either way there is no Info.plist, which means no icon and no name: the
    # application shows the grey placeholder in the Dock, labelled "pf". exec
    # fixes it because the replacement process image is the bundle's own
    # executable, at its own path.
    install(PROGRAMS "${PROJECT_SOURCE_DIR}/cmake/pf-wrapper.sh"
        DESTINATION ${CMAKE_INSTALL_BINDIR}
        RENAME pf)

    # These have to match the bundle the target actually produces, which they
    # did not: the bundle installed as pf.app while its themes and icon went
    # into a Panefile.app no rule ever created. The result was an installed
    # application with neither, sitting beside an empty directory holding both.
    # The target is now named Panefile, so both ends say Panefile.app.
    install(DIRECTORY "${PROJECT_SOURCE_DIR}/data/themes"
        DESTINATION "Panefile.app/Contents/Resources")

    # The bundle target already carries the .icns through
    # MACOSX_PACKAGE_LOCATION, but an install of the loose binary — which is
    # what `cmake --install` produces alongside it — has no bundle to carry it.
    install(FILES "${PROJECT_SOURCE_DIR}/data/icons/panefile.icns"
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
