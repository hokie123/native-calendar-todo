# Changelog

## Agent Skill 0.2.0 — Bootstrap flow (2026-11-14)

- Added a self-contained Agent Skill bootstrap with machine-readable environment detection.
- Bundled the Windhawk mod source inside the Skill package.
- Added Python and Windhawk prerequisite handling after explicit user approval.
- Added a Windhawk CLI installation path and a GUI fallback when no usable CLI is available.
- Added optional Codex and CodeBuddy Skill-directory installation helpers.
- Added GitHub Actions checks for PowerShell syntax, Python syntax, and bundled source synchronization.
- Reworked Chinese documentation around Agent-driven installation and final `doctor + list` verification.

## 0.4.9 — Clean baseline (2026-10-09)

- Simplified the previous experimental fork to keep the to-do experience and statutory holiday/makeup-workday calendar markers.
- Removed unfinished ChinaCalendar local ICS parsing, calendar term handling, and local holiday cache paths.
- Preserved the existing Windhawk mod ID and task storage compatibility.
- Prepared the first public-repository layout; this snapshot still needs final compile validation.

Earlier experimental iterations (0.1–0.4.8) are not published as stable releases.
