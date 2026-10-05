# Lessons

## A misleading status message usually means the feature is missing, not the message wrong

When a build summary or UI says a feature is off for the wrong reason, check
whether the user wants the feature built before rewording the message. The
configure summary blamed a missing dependency for SYNTAX/MEDIA/PDF/VIDEO_THUMBS
because the plugin host did not exist; the fix wanted was to build the plugins,
not to relabel them "not implemented". If a task brief and the underlying goal
might differ, ask which one is wanted before changing anything.
