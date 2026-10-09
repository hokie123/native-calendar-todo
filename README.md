# Native Calendar Todo for Windows 11

A community-maintained, experimental **Windhawk mod** that adds an editable to-do list to the Windows 11 calendar/notification-center flyout, without replacing the familiar calendar interface.

> **Status:** v0.4.9 source snapshot. Built and exercised on a user's Windows 11 installation in earlier iterations, but **this clean release has not yet been independently compile-tested** on a Windows/Windhawk environment. Windows updates may break Shell hooks.

## Features

- To-do tasks associated with specific calendar dates
- Create, edit, complete, and delete tasks
- Meeting details, notes, links, and user-defined categories/colors
- Colored task-density marks in calendar dates
- China statutory holiday and makeup-workday markers via [CN-Holidays](https://github.com/cfap/CN-Holidays) ICS feed
  - **Holiday:** red `#B42336`
  - **Makeup workday:** blue `#0070C0`
- In-process ICS caching and Windhawk per-mod task storage

### Not included in this version

- Traditional festivals, lunar dates, solar terms, or ChinaCalendar feeds
- Reliable offline ICS persistence (holiday feed requires network after Shell restart)
- Cloud task synchronization, notifications, or an independent executable

## Installation

1. Install [Windhawk](https://windhawk.net/).
2. Open Windhawk and use **Create a new mod** (enable developer mode if the option is unavailable).
3. Open [`native-calendar-todo.wh.cpp`](native-calendar-todo.wh.cpp) as plain text and copy its **entire contents** into the Windhawk mod editor.
4. Click **Compile Mod** or press `Ctrl+B` and enable the mod if compilation succeeds.
5. Open the Windows 11 calendar using `Win+N` or the taskbar clock.

If upgrading an existing installation, **back up the current source first** and replace it in the same mod rather than installing a second copy. The mod ID remains `win11-native-todo-calendar` to retain existing Windhawk task data. Avoid running simultaneously with the upstream Agenda mod; both may modify the same Shell controls.

**Compatibility:** targets `ShellExperienceHost.exe` and `ShellHost.exe` on x64 Windows 11. This is an unofficial Windows Shell customization, not a Microsoft or Windhawk first-party product.

## Updating this project

Edit `native-calendar-todo.wh.cpp`, compile with Windhawk, test `Win+N`, then commit. When publishing to your own repository, update the Windhawk header's `@github` to your repository URL; preserve upstream copyright/license notices.

```powershell
git add .
git commit -m "Describe your change"
git push
```

## Data & privacy

Task content is stored locally by Windhawk under the mod's existing storage keys. This repository contains source code only: **do not commit** personal task backups, `.ics` exports, credentials, or private meeting URLs.

## Known limitations

- Windows feature updates may alter XAML visual trees or Shell behavior and require changes to hooks.
- Holiday ICS content depends on a third-party source, and network access may be needed at first launch.
- This project is experimental; disable the mod in Windhawk to revert the Shell UI if something misbehaves.

## Credits and license

Derived from **[Agenda in Calendar View](https://windhawk.net/mods/agenda-in-calendar-view)** by `lonfro`, with upstream Shell hooking work attributed to `m417z`. This repository retains the existing copyright notices and is distributed under **GNU GPL v3** (see [`LICENSE`](LICENSE)). Please preserve author attributions when redistributing.

## Feedback

For reproducible bugs, please open an issue with your Windows build, Windhawk version, steps to reproduce, expected/actual results, and relevant non-sensitive logs. Do not post tokens or private task data.
