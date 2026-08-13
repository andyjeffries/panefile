#!/bin/sh
# Panefile's command-line entry point on macOS.
#
# A wrapper rather than a copy of the binary, and rather than a symlink to it,
# because macOS decides what an application *is* from where its executable
# lives. CFBundleGetMainBundle walks up from the running executable's path
# looking for Contents/MacOS, and both a second copy in bin/ and a symlink
# pointing into the bundle defeat it — a symlink because the kernel reports the
# path the process was launched by, not the path it resolves to.
#
# Without a bundle there is no Info.plist, so the application has no icon and no
# name: it appears in the Dock as the grey placeholder, labelled "pf".
#
# The bundle is found relative to this script rather than baked in at configure
# time, so the whole prefix can be moved or staged into a package root and this
# still points at the right place.
here=$(cd -- "$(dirname -- "$0")" && pwd -P)
exec "$here/../Panefile.app/Contents/MacOS/Panefile" "$@"
