# Lessons

## A misleading status message usually means the feature is missing, not the message wrong

When a build summary or UI says a feature is off for the wrong reason, check
whether the user wants the feature built before rewording the message. The
configure summary blamed a missing dependency for SYNTAX/MEDIA/PDF/VIDEO_THUMBS
because the plugin host did not exist; the fix wanted was to build the plugins,
not to relabel them "not implemented". If a task brief and the underlying goal
might differ, ask which one is wanted before changing anything.

## Capture a reference app the way the user actually runs it

A headless Nautilus capture used its default list zoom at 2x scale, so its
rows looked twice the height of the user's real Nautilus and nearly misled a
density decision. When comparing against another application, match the user's
own settings (zoom, scale, theme) or ask for a screenshot of theirs, and say
which one a comparison is based on.
