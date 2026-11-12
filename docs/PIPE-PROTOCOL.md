# Pipe protocol

The companion service exposes the task list on
`\\.\pipe\LOCAL\NativeCalendarTodo.v1`. This is the contract `calctl.py` implements; you
only need it to write your own client.

## Framing

A connection carries one request. Both directions are a little-endian `uint32` byte
count followed by that many UTF-8 bytes. Replies start with `OK\n` on success.

## Requests

- `LIST` — return every task.
- `REPLACE\n<payload>` — replace the whole list with `<payload>`.

There is no per-item write: read with `LIST`, change what you need, send the full list
back. Do it in one short window; re-read if another client wrote in between.

## Payload

`#v3\n` followed by one line per task, fields joined by tabs and `%`-escaped
(`%25`, `%09`, `%0A`, `%0D`):

```
id  day  done(1|0)  categoryId  title  time  location  url  notes  remindMinutes  timezone  provider  extId  revision
```

- `day` is `YYYYMMDD`; `time` is `HH:MM` or `HH:MM-HH:MM`; `remindMinutes` is empty when
  no reminder is wanted.
- Lines that do not decode to 14 fields, or whose `day` is not 8 characters, are skipped.

## Fallback copy

The mod keeps its own copy under
`HKLM\SOFTWARE\Windhawk\Engine\ModsWritable\local@win11-native-todo-calendar\LocalStorage`
(value `todo-tasks-v3`, same payload). It reads that copy when the pipe is unreachable,
so after a write keep both sides identical. A missing key is not an error — another mod
id or a machine without Windhawk simply has no fallback.

## Storage

The service owns `%LOCALAPPDATA%\NativeCalendarTodo\tasks.sqlite3`
(`tasks`, `notification_queue`, `notification_deliveries`, `daily_review_deliveries`).
Read it if you only need to inspect, but write through the pipe so the service can
schedule notifications and keep its state consistent.
