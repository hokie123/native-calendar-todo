#!/usr/bin/env python3
"""calctl -- command-line bridge from local AI tools to the Windows 11 calendar to-do list.

The calendar to-do list is drawn by the Windhawk mod (native-calendar-todo.wh.cpp) and
persisted by its companion service (NativeCalendarTodo.Service.exe). This script speaks
the same local named pipe the mod uses, so an agent can list, add, edit, complete and
delete tasks without touching the Shell UI.

Only the Python standard library is required. Windows only.

Quick start:
    python calctl.py doctor
    python calctl.py list
    python calctl.py add --title "Call grandma" --day 20261108 --time 13:15 --remind 0

Machine-readable output (for agents):
    python calctl.py list --json
"""

from __future__ import annotations

import argparse
import ctypes
import datetime
import json
import os
import re
import struct
import sys
import uuid
from ctypes import wintypes

DEFAULT_PIPE = r"LOCAL\NativeCalendarTodo.v1"
DEFAULT_SHADOW_KEY = (
    r"SOFTWARE\Windhawk\Engine\ModsWritable"
    r"\local@win11-native-todo-calendar\LocalStorage"
)
SHADOW_VALUE = "todo-tasks-v3"
PENDING_VALUE = "todo-pending-sync-v1"

PIPE_NAME = os.environ.get("CALCTL_PIPE", DEFAULT_PIPE)
SHADOW_KEY = os.environ.get("CALCTL_SHADOW_KEY", DEFAULT_SHADOW_KEY)


# --------------------------------------------------------------------------- pipe
class PipeError(RuntimeError):
    pass


def _request(text: str, timeout_ms: int = 5000) -> str:
    """Send one request over the service pipe and return the raw reply (``OK\\n...``)."""
    path = "\\\\.\\pipe\\" + PIPE_NAME
    GENERIC_READ, GENERIC_WRITE, OPEN_EXISTING = 0x80000000, 0x40000000, 3
    INVALID_HANDLE_VALUE = ctypes.c_void_p(-1).value

    k32 = ctypes.WinDLL("kernel32", use_last_error=True)
    k32.WaitNamedPipeW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD]
    k32.WaitNamedPipeW.restype = wintypes.BOOL
    k32.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
                                wintypes.LPVOID, wintypes.DWORD, wintypes.DWORD,
                                wintypes.HANDLE]
    k32.CreateFileW.restype = wintypes.HANDLE
    k32.ReadFile.argtypes = [wintypes.HANDLE, wintypes.LPVOID, wintypes.DWORD,
                             ctypes.POINTER(wintypes.DWORD), wintypes.LPVOID]
    k32.ReadFile.restype = wintypes.BOOL
    k32.WriteFile.argtypes = [wintypes.HANDLE, wintypes.LPCVOID, wintypes.DWORD,
                              ctypes.POINTER(wintypes.DWORD), wintypes.LPVOID]
    k32.WriteFile.restype = wintypes.BOOL
    k32.CloseHandle.argtypes = [wintypes.HANDLE]

    if not k32.WaitNamedPipeW(path, timeout_ms):
        raise PipeError("pipe not available (is NativeCalendarTodo.Service.exe running?)")
    handle = k32.CreateFileW(path, GENERIC_READ | GENERIC_WRITE, 0, None,
                             OPEN_EXISTING, 0, None)
    if handle == INVALID_HANDLE_VALUE:
        raise PipeError("cannot open pipe (error %d)" % ctypes.get_last_error())
    try:
        payload = text.encode("utf-8")
        written = wintypes.DWORD(0)
        if not k32.WriteFile(handle, struct.pack("<I", len(payload)), 4,
                             ctypes.byref(written), None):
            raise PipeError("write failed (error %d)" % ctypes.get_last_error())
        if payload and not k32.WriteFile(handle, payload, len(payload),
                                         ctypes.byref(written), None):
            raise PipeError("write failed (error %d)" % ctypes.get_last_error())

        buf = ctypes.create_string_buffer(4)
        read = wintypes.DWORD(0)
        if not k32.ReadFile(handle, buf, 4, ctypes.byref(read), None):
            raise PipeError("read failed (error %d)" % ctypes.get_last_error())
        (size,) = struct.unpack("<I", buf.raw)
        if size > 4 * 1024 * 1024:
            raise PipeError("reply too large")
        out = ctypes.create_string_buffer(size) if size else ctypes.create_string_buffer(1)
        got = wintypes.DWORD(0)
        if size and not k32.ReadFile(handle, out, size, ctypes.byref(got), None):
            raise PipeError("read failed (error %d)" % ctypes.get_last_error())
        return out.raw[:size].decode("utf-8")
    finally:
        k32.CloseHandle(handle)


# -------------------------------------------------------------------------- codec
def _esc(value: str) -> str:
    return (value.replace("%", "%25").replace("\t", "%09")
                 .replace("\n", "%0A").replace("\r", "%0D"))


def _unesc(value: str) -> str:
    table = {"25": "%", "09": "\t", "0A": "\n", "0D": "\r"}
    out, i = [], 0
    while i < len(value):
        if value[i] == "%" and i + 2 < len(value):
            hit = table.get(value[i + 1:i + 3].upper())
            if hit is not None:
                out.append(hit)
                i += 3
                continue
        out.append(value[i])
        i += 1
    return "".join(out)


def _parse(reply: str) -> list[dict]:
    if not reply.startswith("OK\n"):
        raise PipeError("unexpected reply: %r" % reply[:80])
    body = reply[3:]
    if not body.startswith("#v3\n"):
        raise PipeError("unexpected payload header")
    tasks = []
    for line in body[4:].split("\n"):
        if not line:
            continue
        col = line.split("\t")
        if len(col) != 14 or not col[0] or len(col[1]) != 8 or not col[4]:
            continue
        tasks.append({
            "id": _unesc(col[0]),
            "day": col[1],
            "done": col[2] == "1",
            "category": _unesc(col[3]),
            "title": _unesc(col[4]),
            "time": _unesc(col[5]),
            "location": _unesc(col[6]),
            "url": _unesc(col[7]),
            "notes": _unesc(col[8]),
            "remind": int(col[9]) if col[9] else -1,
            "tz": col[10],
            "provider": col[11],
        })
    return tasks


def _serialize(tasks: list[dict]) -> str:
    rows = ["#v3"]
    for t in tasks:
        rows.append("\t".join([
            _esc(t["id"]), t["day"], "1" if t["done"] else "0",
            _esc(t.get("category") or "work"), _esc(t["title"]),
            _esc(t.get("time") or ""), _esc(t.get("location") or ""),
            _esc(t.get("url") or ""), _esc(t.get("notes") or ""),
            str(t["remind"]) if t.get("remind", -1) >= 0 else "",
            t.get("tz") or "", t.get("provider") or "local", "", "",
        ]))
    return "\n".join(rows) + "\n"


# ----------------------------------------------------------------------- storage
def load() -> list[dict]:
    return _parse(_request("LIST"))


def save(tasks: list[dict]) -> None:
    """Replace the whole list. The pipe has no per-item write, so always merge first."""
    reply = _request("REPLACE\n" + _serialize(tasks))
    if not reply.startswith("OK\n"):
        raise PipeError("service rejected the update: %r" % reply[:120])
    sync_shadow(tasks)


def sync_shadow(tasks: list[dict]) -> None:
    """Mirror the list into the mod's fallback copy.

    The mod keeps its own copy for when the pipe is unreachable. If the two sides
    disagree, the tray calendar shows stale tasks, so best-effort mirror after
    every write. Missing registry key (another mod id, no Windhawk) is not fatal.
    """
    try:
        import winreg
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, SHADOW_KEY, 0,
                            winreg.KEY_SET_VALUE) as key:
            winreg.SetValueEx(key, SHADOW_VALUE, 0, winreg.REG_SZ, _serialize(tasks))
    except (ImportError, OSError):
        pass


# ------------------------------------------------------------------------ helpers
def fmt_day(day: str) -> str:
    return "%s-%s-%s" % (day[:4], day[4:6], day[6:8])


def pick(tasks: list[dict], task_id: str | None, match: str | None) -> dict:
    if task_id:
        want = task_id.strip("{}").upper()
        hits = [t for t in tasks if t["id"].upper() == want]
    else:
        needle = (match or "").lower()
        hits = [t for t in tasks if needle and needle in t["title"].lower()]
    if len(hits) != 1:
        raise SystemExit("matched %d tasks; need exactly one" % len(hits))
    return hits[0]


def normalize_day(value: str | None) -> str:
    if not value:
        return datetime.date.today().strftime("%Y%m%d")
    if not re.fullmatch(r"\d{8}", value):
        raise SystemExit("--day must be YYYYMMDD")
    return value


def normalize_time(value: str | None) -> str:
    text = (value or "").replace("：", ":").strip()
    if text and not re.fullmatch(r"\d{1,2}:\d{2}(-\d{1,2}:\d{2})?", text):
        raise SystemExit("--time must look like 14:00 or 14:00-15:30")
    return text


# ----------------------------------------------------------------------- commands
def cmd_list(args) -> int:
    tasks = load()
    if args.day:
        tasks = [t for t in tasks if t["day"] == args.day]
    if args.pending:
        tasks = [t for t in tasks if not t["done"]]
    tasks.sort(key=lambda t: (t["day"], t["time"]))
    if args.json:
        print(json.dumps(tasks, ensure_ascii=False, indent=2))
        return 0
    if not tasks:
        print("(no tasks)")
        return 0
    for t in tasks:
        when = fmt_day(t["day"]) + ((" " + t["time"]) if t["time"] else "")
        if t["remind"] < 0:
            remind = ""
        elif t["remind"] == 0:
            remind = "  [remind on time]"
        else:
            remind = "  [remind %d min before]" % t["remind"]
        print("%s %s  %s  (%s)%s  id=%s" % (
            "x" if t["done"] else " ", when, t["title"], t["category"], remind, t["id"]))
    return 0


def cmd_add(args) -> int:
    tasks = load()
    task = {
        "id": str(uuid.uuid4()).upper(),
        "day": normalize_day(args.day),
        "done": False,
        "category": args.category or "work",
        "title": args.title,
        "time": normalize_time(args.time),
        "location": args.location or "",
        "url": args.url or "",
        "notes": args.notes or "",
        "remind": int(args.remind) if args.remind is not None else -1,
    }
    tasks.append(task)
    save(tasks)
    print(json.dumps(task, ensure_ascii=False) if args.json
          else "added %s  %s %s  id=%s" % (fmt_day(task["day"]), task["time"],
                                           task["title"], task["id"]))
    return 0


def cmd_edit(args) -> int:
    tasks = load()
    task = pick(tasks, args.id, args.match)
    if args.title:
        task["title"] = args.title
    if args.day:
        task["day"] = normalize_day(args.day)
    if args.time is not None:
        task["time"] = normalize_time(args.time)
    if args.category:
        task["category"] = args.category
    if args.notes is not None:
        task["notes"] = args.notes
    if args.remind is not None:
        task["remind"] = int(args.remind)
    save(tasks)
    print(json.dumps(task, ensure_ascii=False) if args.json
          else "updated %s  %s %s" % (fmt_day(task["day"]), task["time"], task["title"]))
    return 0


def cmd_done(args) -> int:
    tasks = load()
    task = pick(tasks, args.id, args.match)
    task["done"] = not args.undo
    save(tasks)
    print("%s %s" % ("reopened" if args.undo else "completed", task["title"]))
    return 0


def cmd_rm(args) -> int:
    tasks = load()
    task = pick(tasks, args.id, args.match)
    save([t for t in tasks if t["id"] != task["id"]])
    print("removed %s" % task["title"])
    return 0


def cmd_doctor(args) -> int:
    checks, ok = [], True

    def report(name, good, detail=""):
        nonlocal ok
        ok = ok and good
        checks.append({"check": name, "ok": good, "detail": detail})

    try:
        tasks = load()
        report("pipe", True, "%d task(s) readable" % len(tasks))
    except Exception as exc:  # noqa: BLE001 - doctor reports, never raises
        tasks = []
        report("pipe", False, str(exc))

    try:
        import winreg
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, SHADOW_KEY, 0, winreg.KEY_READ):
            report("mod storage", True, SHADOW_KEY)
    except (ImportError, OSError) as exc:
        report("mod storage", False, "%s (%s)" % (SHADOW_KEY, exc))

    report("python", sys.version_info >= (3, 8), sys.version.split()[0])
    report("platform", sys.platform == "win32", sys.platform)

    if args.json:
        print(json.dumps({"ok": ok, "checks": checks}, ensure_ascii=False, indent=2))
    else:
        for c in checks:
            print("[%s] %-12s %s" % ("ok" if c["ok"] else "!!", c["check"], c["detail"]))
        print("\nready" if ok else "\nnot ready: fix the checks marked !!")
    return 0 if ok else 1


def main() -> int:
    parser = argparse.ArgumentParser(prog="calctl", description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("list", help="list tasks")
    p.add_argument("--json", action="store_true")
    p.add_argument("--day", help="only this day (YYYYMMDD)")
    p.add_argument("--pending", action="store_true", help="hide completed tasks")
    p.set_defaults(func=cmd_list)

    p = sub.add_parser("add", help="add a task")
    p.add_argument("--title", required=True)
    p.add_argument("--day", help="YYYYMMDD, defaults to today")
    p.add_argument("--time", help="14:00 or 14:00-15:30")
    p.add_argument("--category", default="work")
    p.add_argument("--notes")
    p.add_argument("--location")
    p.add_argument("--url")
    p.add_argument("--remind", help="remind N minutes before the time")
    p.add_argument("--json", action="store_true")
    p.set_defaults(func=cmd_add)

    p = sub.add_parser("edit", help="change an existing task")
    p.add_argument("--id")
    p.add_argument("--match", help="unique substring of the title")
    p.add_argument("--title")
    p.add_argument("--day")
    p.add_argument("--time")
    p.add_argument("--category")
    p.add_argument("--notes")
    p.add_argument("--remind")
    p.add_argument("--json", action="store_true")
    p.set_defaults(func=cmd_edit)

    p = sub.add_parser("done", help="mark done")
    p.add_argument("--id")
    p.add_argument("--match")
    p.add_argument("--undo", action="store_true", help="reopen instead")
    p.set_defaults(func=cmd_done)

    p = sub.add_parser("rm", help="delete a task")
    p.add_argument("--id")
    p.add_argument("--match")
    p.set_defaults(func=cmd_rm)

    p = sub.add_parser("doctor", help="check that everything is wired up")
    p.add_argument("--json", action="store_true")
    p.set_defaults(func=cmd_doctor)

    args = parser.parse_args()
    try:
        return args.func(args)
    except PipeError as exc:
        print("error: %s" % exc, file=sys.stderr)
        print("run `python calctl.py doctor` for a full check", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
