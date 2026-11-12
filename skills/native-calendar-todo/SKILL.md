---
name: native-calendar-todo
description: Read and write the Windows 11 taskbar calendar to-do list (本机日历待办) through the Native Calendar Todo companion service. Use when the user wants to add, view, move, complete or delete calendar tasks or reminders from an agent session instead of clicking the flyout.
---

# Native Calendar Todo

The Windows 11 calendar flyout can hold per-day to-dos drawn by the
`native-calendar-todo` Windhawk mod. Its companion service owns the data and exposes it
on the local pipe `\\.\pipe\LOCAL\NativeCalendarTodo.v1`; `scripts/calctl.py` is a
standard-library-only client for that pipe. Tasks created here appear in the flyout, and
tasks the user ticks off are readable here.

## Check first

```powershell
python scripts/calctl.py doctor
```

Exit `0` ready · `1` a check failed (usually the service is not running) · `2` pipe
unusable. Report a failed check instead of guessing, and never claim a write succeeded
without the command's output.

## Commands

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

`--match` takes a unique substring of the title and fails unless exactly one task
matches; use it when you do not already hold the ID.

## Rules

1. Dates are absolute `YYYYMMDD`. Resolve "tomorrow" against the local date and say which
   date you used.
2. Do not create a task for today unless the user said today; after midnight, confirm the
   date rather than guessing.
3. `remind` is minutes before the start time — `0` at that time, `1440` one day before,
   omitted none. The toast comes from the companion service, so it fires without the
   agent session, but not while the machine is off.
4. Read the affected day back after every write.
5. A write replaces the whole list: no concurrent clients, back up `tasks.sqlite3`
   before bulk changes.
