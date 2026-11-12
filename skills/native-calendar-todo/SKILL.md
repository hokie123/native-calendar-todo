---
name: native-calendar-todo
description: Read and write the Windows 11 taskbar calendar to-do list through the Native Calendar Todo companion service. Use when the user wants to add, view, move, complete or delete calendar tasks or reminders from an agent session instead of clicking the flyout.
---

# Native Calendar Todo

The Windows 11 calendar flyout (taskbar clock) can hold per-day to-dos drawn by the
`native-calendar-todo` Windhawk mod. Its companion service owns the data and the toast
notifications, and exposes both on the local pipe
`\\.\pipe\LOCAL\NativeCalendarTodo.v1`. `scripts/calctl.py` is a standard-library-only
client for that pipe, so tasks created here appear in the flyout immediately, and tasks
the user ticks off are readable here.

## Before anything else

Run the health check and stop if it fails:

```powershell
python scripts/calctl.py doctor
```

- Exit `0` — ready, continue.
- Exit `1` — a check failed; report which one to the user. The usual cause is that
  `NativeCalendarTodo.Service.exe` is not running or the mod is not installed.
- Exit `2` — the pipe could not be used at all.

Never invent task data or claim a write succeeded without the command's output.

## Commands

Use `--json` so results are machine-readable.

```powershell
python scripts/calctl.py list --json                 # everything
python scripts/calctl.py list --json --day 20261108  # one day
python scripts/calctl.py list --json --pending       # unfinished only
python scripts/calctl.py add --title "Call grandma" --day 20261108 --time 13:15 --remind 0 --json
python scripts/calctl.py edit --match "grandma" --day 20261109 --json
python scripts/calctl.py done --match "grandma"
python scripts/calctl.py done --match "grandma" --undo
python scripts/calctl.py rm --match "grandma"
```

`--match` accepts a unique substring of the title and fails unless exactly one task
matches; prefer it when you do not already hold the task ID.

## Rules

1. Dates are absolute `YYYYMMDD`. Resolve "tomorrow", "next Friday" and similar against
   the current local date, then state the date you used in your reply.
2. Do not schedule a task for today unless the user explicitly said today; when the
   user speaks late at night, "tomorrow" usually means the coming day, so confirm the
   date in one short clause instead of guessing silently.
3. `remind` is minutes before the start time — `0` means at that time, `1440` means one
   day before, omitted means no reminder. The notification is a Windows toast sent by
   the companion service, so it fires without the agent session running; it does not
   fire while the machine is off.
4. After every write, read the affected day back (`list --day`) and confirm the change.
5. A write rewrites the whole list (`REPLACE`). Do not run other clients concurrently
   against the same store, and back up `tasks.sqlite3` before bulk changes.

## Installing the skill

Copy this folder into the skills directory of the agent host, for example:

- Claude Code — `~/.claude/skills/native-calendar-todo/`
- Codex — reference `SKILL.md` and `scripts/calctl.py` from `AGENTS.md`
- DS Bot — add the folder as a team skill

The prerequisites are the Windhawk mod plus its companion service; without them the
pipe does not exist and `doctor` will say so.
