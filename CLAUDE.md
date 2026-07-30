# Project rules

These rules apply to every session on this repo. Follow them without being reminded.

## Git commits require explicit approval

Never run `git commit` on your own initiative. Always ask first and wait for explicit confirmation before committing — even when wrapping up a session or completing a doc sweep.

## Specs orient you; journals track progress

The repo has three subprojects (marvin, fretboard, fret-tuner) plus a top-level system spec.

- **[`SPEC.md`](SPEC.md) is the cross-subproject overview** — read it first if a task spans subprojects or if you're new to the repo. It points at each subproject's spec and journal.
- For non-trivial work inside a single subproject, read that subproject's spec **and** journal first (see below).

## Journals are the source of truth for planning and progress

This repo uses working journals to track planning, decisions, open questions, and work-in-progress *outside* the chat context so they survive across sessions, compactions, and restarts.

Current journals:

- `firmware/marvin/docs/journal.md` — marvin firmware (SAM9X75 vision-based guitar-playing robot)
  - System-level spec for marvin lives at `firmware/marvin/docs/spec.md` — **read it first** alongside the journal on any non-trivial marvin task. The spec is the durable description of what marvin is (purpose, subsystems, interfaces, milestones); the journal is the running diary.
- `firmware/fretboard/docs/journal.md` — fretboard firmware (PIC32CM phototransistor **detector** node: ADC stream out; re-scoping from sensor/actuator — the actuator role is moving to the `guitar` subproject, but fretboard still actuates until that node is proven)
  - Spec for fretboard lives at `firmware/fretboard/SPEC.md` — read it alongside the journal on any non-trivial fretboard task.
- `firmware/guitar/docs/journal.md` — guitar firmware (PIC32CM PL10 Wii-guitar **actuator** node: receives marvin's button bitmask over T1S → open-drain GPIO; T1S PLCA follower id 2. Working on hardware — follower up, marvin drives it over T1S, presence heartbeat + CLI)
  - Spec for guitar lives at `firmware/guitar/SPEC.md` — read it alongside the journal on any non-trivial guitar task.
- `firmware/lemmy/docs/journal.md` — lemmy firmware (PIC32CM PL10 **animation** node: animated guitar-playing puppet, two R/C servos — neck nod + jaw; T1S PLCA follower id 6. Bring-up — base MCC project scaffolded; T1S follower first, motion second)
  - Spec for lemmy lives at `firmware/lemmy/SPEC.md` — read it alongside the journal on any non-trivial lemmy task.
- `firmware/lightshow/docs/journal.md` — lightshow firmware (PIC32CM PL10 **lighting** node: drives LEDs/lamps in time to the music; T1S PLCA follower id 7, `node_type = 5`. Bring-up — bootstrapped from lemmy's T1S follower; T1S follower first, LED output second)
  - Spec for lightshow lives at `firmware/lightshow/SPEC.md` — read it alongside the journal on any non-trivial lightshow task.
- `tools/edge-ai/docs/journal.md` — edge-ai (distill marvin's gameplay commands into a small ML model for the fretboard MCU)
  - Specs for edge-ai live under `tools/edge-ai/docs/` (`SPEC.md` + the doc map there) — read them alongside the journal on any non-trivial edge-ai task.
- `firmware/fauxmote/docs/journal.md` — fauxmote (ESP32 firmware emulating a Wiimote + guitar extension to a real Wii; parallel proof-of-concept)
  - Spec for fauxmote lives at `firmware/fauxmote/SPEC.md` — read it alongside the journal on any non-trivial fauxmote task.
- `tools/gameplay/docs/journal.md` — gameplay (offline host-side prototype for marvin's GH3 game-state observer/controller; algorithms proven against the screen corpus, then ported to a firmware `gameplay_engine`)
  - Orienting docs: marvin `spec.md` §4.8 and `firmware/marvin/docs/gh3_navigation.md` — read them alongside the journal on any non-trivial gameplay task.

When working on a subproject that has a journal:

1. **Read the journal first.** Before responding to any non-trivial request, read the journal for that subproject. It contains the current focus, phased plan, open questions, decision log, and session log — the state you need to be useful.
2. **Update the journal as you go.** When we make a decision, resolve an open question, finish a plan phase, or finish a session, reflect that in the journal. Newest entries at the top within each section.
   - Decisions → append to the decision log with date and rationale.
   - Resolved open questions → remove from "Open questions" and add to the decision log with the answer and rationale.
   - Plan progress → check off phase items; add sub-bullets for anything learned during implementation.
   - Session close → add a dated session-log entry summarizing what happened.
3. **Prefer updating the journal over restating plans in chat.** If I'd otherwise write a multi-paragraph plan in chat, it belongs in the journal — the chat is ephemeral, the journal isn't.
4. **If a new subproject needs a journal, create one** using the same structure as `firmware/marvin/docs/journal.md`, and add it to the list above in this file.

## Do not modify MCC-generated or Legato library code

Files under `firmware/*/default/src/config/default/` are owned by MPLAB Code Configurator (MCC) or the Legato GFX library. Do **not** edit them — MCC regeneration can overwrite changes silently, and modified vendor files are invisible as customizations.

If a task requires changing behavior in this tree, stop and ask how to proceed. Typical alternatives: add a new file in the project's own source tree that wraps or extends the vendor behavior, use a Legato extension point (e.g. `leDrawSurfaceWidget`, a custom skin registered via the vtable), or adjust the MCC configuration and regenerate rather than hand-editing the output.

## Do not use `.specstory/` history as context

The `.specstory/` directory holds raw transcripts of past chat sessions. Do **not** read or treat those files as authoritative context — they are ephemeral records, not source of truth, and may contradict the current code or journals. When you need prior state, use the journals, specs, code, and git history instead.

## Code comments describe current code only

Comments in source files explain **what the code currently does and why**, when that's non-obvious. They are not a development diary.

Do not put any of the following in source comments — they belong in the journal, commit messages, or PR descriptions:

- Narrative of how the code evolved ("previously we did X, now we do Y", "this replaces the old …")
- Debug notes, troubleshooting history, or why something *doesn't* work
- TODO lists for future features (use the journal or an issue tracker)
- References to tasks, tickets, or past conversations ("fix for Greg's bug", "see chat from 2026-05-01")
- Extensive block comments explaining design rationale — a one-line "why" is fine; multi-paragraph prose goes in the journal

**Default to no comment.** Only add one when removing it would confuse a future reader. If you catch yourself wanting to write a paragraph, write it in the journal instead and link from the code if needed (e.g. `// see firmware/marvin/docs/journal.md`).
