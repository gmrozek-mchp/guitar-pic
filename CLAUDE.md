# Project rules

These rules apply to every session on this repo. Follow them without being reminded.

## Journals are the source of truth for planning and progress

This repo uses working journals to track planning, decisions, open questions, and work-in-progress *outside* the chat context so they survive across sessions, compactions, and restarts.

Current journals:

- `firmware/marvin/docs/journal.md` — marvin firmware (SAM9X75 vision-based guitar-playing robot)

When working on a subproject that has a journal:

1. **Read the journal first.** Before responding to any non-trivial request, read the journal for that subproject. It contains the current focus, phased plan, open questions, decision log, and session log — the state you need to be useful.
2. **Update the journal as you go.** When we make a decision, resolve an open question, finish a plan phase, or finish a session, reflect that in the journal. Newest entries at the top within each section.
   - Decisions → append to the decision log with date and rationale.
   - Resolved open questions → remove from "Open questions" and add to the decision log with the answer and rationale.
   - Plan progress → check off phase items; add sub-bullets for anything learned during implementation.
   - Session close → add a dated session-log entry summarizing what happened.
3. **Prefer updating the journal over restating plans in chat.** If I'd otherwise write a multi-paragraph plan in chat, it belongs in the journal — the chat is ephemeral, the journal isn't.
4. **If a new subproject needs a journal, create one** using the same structure as `firmware/marvin/docs/journal.md`, and add it to the list above in this file.

## Code comments describe current code only

Comments in source files explain **what the code currently does and why**, when that's non-obvious. They are not a development diary.

Do not put any of the following in source comments — they belong in the journal, commit messages, or PR descriptions:

- Narrative of how the code evolved ("previously we did X, now we do Y", "this replaces the old …")
- Debug notes, troubleshooting history, or why something *doesn't* work
- TODO lists for future features (use the journal or an issue tracker)
- References to tasks, tickets, or past conversations ("fix for Greg's bug", "see chat from 2026-05-01")
- Extensive block comments explaining design rationale — a one-line "why" is fine; multi-paragraph prose goes in the journal

**Default to no comment.** Only add one when removing it would confuse a future reader. If you catch yourself wanting to write a paragraph, write it in the journal instead and link from the code if needed (e.g. `// see firmware/marvin/docs/journal.md`).
