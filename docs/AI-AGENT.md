# Driving the to-do list from a local AI agent

This page describes how an agent on the same Windows machine can read and write the
calendar to-do list directly, without opening the Shell flyout.

## How it fits together

```
Windows 11 Shell (ShellHost.exe / ShellExperienceHost.exe)
  └─ Windhawk mod  native-calendar-todo.wh.cpp   draws the calendar UI
        │  named pipe  \\.\pipe\LOCAL\NativeCalendarTodo.v1
        ▼
  NativeCalendarTodo.Service.exe    owns the SQLite store and sends the toast notifications
        │
        ▼
  %LOCALAPPDATA%\NativeCalendarTodo\tasks.sqlite3
```

The mod never talks to a cloud service. An agent that speaks the same pipe protocol
becomes another client of the companion service, so it sees exactly what the flyout
shows, and its writes appear in the calendar.

`skills/native-calendar-todo/scripts/calctl.py` is the reference client. It needs nothing but the Python standard
library and Windows.

## Prerequisites

1. The Windhawk mod is installed and enabled (see the repository README).
2. The companion service `NativeCalendarTodo.Service.exe` is running, so the pipe
   `\\.\pipe\LOCAL\NativeCalendarTodo.v1` exists.
3. Python 3.8+ on `PATH` (`python calctl.py doctor` tells you whether you are ready).

Check the environment first — never guess:

```powershell
python skills\native-calendar-todo\scripts\calctl.py doctor
```

```
[ok] pipe         6 task(s) readable
[ok] mod storage  SOFTWARE\Windhawk\Engine\ModsWritable\local@win11-native-todo-calendar\LocalStorage
[ok] python       3.12.14
[ok] platform     win32

ready
```

Exit code is `0` when everything is wired up, `1` when a check failed, `2` when the
pipe could not be used at all.

## Commands for agents

Always prefer `--json`; the plain output is for humans.

| Goal | Command |
| --- | --- |
| everything | `python skills\native-calendar-todo\scripts\calctl.py list --json` |
| one day | `python skills\native-calendar-todo\scripts\calctl.py list --json --day 20261108` |
| open items only | `python skills\native-calendar-todo\scripts\calctl.py list --json --pending` |
| add | `python skills\native-calendar-todo\scripts\calctl.py add --title "Call grandma" --day 20261108 --time 13:15 --remind 0 --json` |
| move / retitle | `python skills\native-calendar-todo\scripts\calctl.py edit --match "Call grandma" --day 20261109 --json` |
| complete | `python skills\native-calendar-todo\scripts\calctl.py done --match "Call grandma"` |
| reopen | `python skills\native-calendar-todo\scripts\calctl.py done --match "Call grandma" --undo` |
| delete | `python skills\native-calendar-todo\scripts\calctl.py rm --match "Call grandma"` |
| health check | `python skills\native-calendar-todo\scripts\calctl.py doctor --json` |

`--match` takes a unique substring of the title and fails unless exactly one task
matches, which is what an agent should use when it does not already hold the ID.

Field notes:

- `day` is `YYYYMMDD` and defaults to today only for `add`.
- `time` is `HH:MM` or `HH:MM-HH:MM`; a full-width colon is normalised.
- `remind` is minutes before the start time, `0` means at that time, omitted means no
  reminder. The companion service turns it into a Windows toast even if the agent
  session has ended.
- `category` is a free-form id; `work` exists by default.

## Protocol, for other clients

Requests and replies are length-prefixed UTF-8: a little-endian `uint32` byte count,
then the payload. A connection carries one request.

- `LIST` → `OK\n#v3\n` followed by one line per task.
- `REPLACE\n<payload>` → `OK\n` and the whole list is replaced.

Payload lines are tab-separated, `%`-escaped (`%25`, `%09`, `%0A`, `%0D`):

```
id  day  done(1|0)  categoryId  title  time  location  url  notes  remindMinutes  timezone  provider  extId  revision
```

There is no per-item write: read with `LIST`, change what you need, send the whole list
back with `REPLACE`. Do that in one short window and re-read on conflict.

The mod also keeps a fallback copy under
`HKLM\SOFTWARE\Windhawk\Engine\ModsWritable\local@win11-native-todo-calendar\LocalStorage`
(value `todo-tasks-v3`). It uses that copy when the pipe is unreachable, so after a
write keep both sides identical — `calctl.py` does this best-effort and ignores a
missing key.

## The bundled skill

[`skills/native-calendar-todo/`](../skills/native-calendar-todo/SKILL.md) is a ready-made
skill package: `SKILL.md` carries the instructions and guards, `scripts/calctl.py` is the
client. Copy that folder into the host's skills directory, for example
`~/.claude/skills/native-calendar-todo/` for Claude Code, and reference it from
`AGENTS.md` for Codex-style hosts.

If you write your own, it only needs:

1. a precondition step that runs `calctl.py doctor` and stops if it is not ready;
2. the command table above;
3. the rule that dates are absolute `YYYYMMDD` and that a task is not created for today
   unless the user asked for today.

The script is self-contained, so a skill can ship it as-is and call it with the
interpreter the host already has.

## Safety

- Everything stays on the machine: the pipe is local-only and the store is a local
  SQLite file.
- A full `REPLACE` rewrites every task; a bug in a client can drop or corrupt tasks.
  Back up `tasks.sqlite3` (or the registry value) before experimenting.
- The form asks for no credentials and this bridge has no network access.
