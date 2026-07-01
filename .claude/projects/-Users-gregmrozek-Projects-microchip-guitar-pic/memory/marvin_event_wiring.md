---
name: marvin-event-wiring
description: Marvin UI now self-registers Legato event handlers; MGS no longer generates event callbacks.
metadata:
  type: project
---

For the marvin Legato UI, event handlers are now **registered by our own code**, not by MGS-generated `event_Marvin_*_On{Pressed,Released}` wiring. Recent MGS regens of the Marvin screen emit **no** auto-event callbacks at all, so any button/panel behavior must be wired explicitly in the screen module's `*_Setup()` via `->fn->setPressedEventCallback` / `setReleasedEventCallback` (see `screen_song_select.c`, `screen_dashboard.c`, and `screen_navigation.c`'s hamburger).

**Why:** the design shifted away from MGS event generation; leftover `event_Marvin_*` functions from the old scheme are orphaned (e.g. the guitar-strum glue in `manual_input.c`, kept intentionally).

**How to apply:** when a widget's behavior is dead after an MGS regen, don't look for a generated event stub — add an explicit `set*EventCallback` in the owning screen's setup. See [[marvin_journal]] (2026-07-01 dashboard redesign entry).
