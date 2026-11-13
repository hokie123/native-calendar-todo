---
name: native-calendar-todo
description: 在 Windows 11 原生日历/通知中心中管理 Native Calendar Todo 待办，并为本地 AI Agent 提供查询、新增、修改、完成、恢复和删除能力。用户提到“日历待办”“Windows 待办”“帮我记到日历”“查看/修改/完成待办”，或要求安装、检测、修复 Native Calendar Todo 时使用。Skill 自带 bootstrap，可检测 Windows、Python、Windhawk、mod 和权限状态，并在获得用户授权后自动完成可自动化的安装步骤。
license: GPL-3.0-only
compatibility: Windows 11。运行时需要 Windhawk 和 Python 3；scripts/bootstrap.ps1 可检测并安装缺失的常规依赖。Windhawk 2 + Development Tools 可通过 CLI 自动安装本地 mod；没有 CLI 的 Windhawk 版本需要 Windhawk UI 或宿主的桌面自动化完成本地 mod 编译。
metadata:
  author: hokie123
  version: "0.2.0"
  repository: "https://github.com/hokie123/native-calendar-todo"
---

# Native Calendar Todo

让本地 AI Agent 直接操作 Windows 11 日历弹窗中的 Native Calendar Todo。Skill 包内同时包含：

- `scripts/calctl.py`：读写待办。
- `scripts/bootstrap.ps1`：检测并部署运行环境。
- `assets/win11-native-todo-calendar.wh.cpp`：可直接安装/编译的 Windhawk mod 源码。
- `references/install-flow.md`：给 Agent 的完整安装状态机。
- `references/storage-format.md`：Windhawk 本地存储格式。

## 第一次使用：先检测

不要先猜测环境。运行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/bootstrap.ps1 -Check -Json
```

读取 JSON 中的 `ok`、`next_action` 和 `manual_action_required`。只有 `ok: true` 才表示运行环境已经完整可用。

## 用户要求“安装这个 Skill / 帮我装好”

先完成宿主自己的 Skill 安装与安全检查，再运行 bootstrap。在任何会修改系统的步骤之前，遵守宿主权限模型。用户明确批准后，可以运行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/bootstrap.ps1 -Apply -Yes -LaunchWindhawk -Json
```

`-Yes` 只表示宿主已经取得用户授权，不能用于绕过授权。

bootstrap 按顺序检查：Windows 11 → Python 3 → Windhawk → Windhawk CLI / Development Tools → Native Calendar Todo mod → `calctl.py doctor` → `calctl.py list`。

### 根据 next_action 继续

- `ready`：完成，可以直接管理待办。
- `install_python`：获得授权后让 bootstrap 安装 Python。
- `install_windhawk`：获得授权后让 bootstrap 通过 winget 安装 Windhawk。
- `install_windhawk_development_tools`：已有 Windhawk CLI，但缺本地编译工具。打开 Windhawk 安装 Development Tools，然后重跑 bootstrap。
- `windhawk_ui_install`：当前 Windhawk 没有可用 CLI。bootstrap 会把完整 mod 源码复制到剪贴板；若宿主支持桌面自动化，继续替用户完成 Create a new mod → 粘贴 → Compile Mod → 启用。否则把这一步明确交给用户。
- `fix_doctor`：组件存在但最终验证失败，根据 `doctor.stderr` 排查。

不要在 `doctor` 与 `list` 未通过时声称“已经安装完成”。完整流程见 `references/install-flow.md`。

## 日常操作

先运行：

```powershell
python scripts/calctl.py doctor --json
```

只有 `ok: true` 才继续。常用命令：

```powershell
python scripts/calctl.py list --json
python scripts/calctl.py list --day 20261115 --json
python scripts/calctl.py list --pending --json
python scripts/calctl.py categories --json

python scripts/calctl.py add --title "给老师回邮件" --day 20261115 --time 09:30 --category work --json
python scripts/calctl.py edit --match "给老师回邮件" --on 20261115 --day 20261116 --time 10:00 --json
python scripts/calctl.py done --match "给老师回邮件" --json
python scripts/calctl.py done --match "给老师回邮件" --undo --json
python scripts/calctl.py rm --match "给老师回邮件" --json
```

## Agent 行为规则

1. 把“今天、明天、下周一”等相对日期解析为本机日期后，再转换成 `YYYYMMDD`；跨午夜附近要特别确认。
2. 新增待办必须显式传入 `--day`，不要因为用户没说日期就默认今天。
3. 写操作前读取相关待办；`--match` 必须唯一，匹配多条时不要猜。
4. `category` 应来自 `categories --json` 返回的 ID。
5. 每次写操作后以命令返回值和重新读取结果为准；失败时不要声称已完成。
6. 不要批量覆盖、清空或重写整个列表，除非用户明确要求。客户端有并发变化检测，避免覆盖用户刚在日历 UI 中做出的修改。
7. 当前没有 Agent 侧提醒/通知能力，不要承诺 `remind`、后台推送或云同步。
8. 写入成功但日历弹窗没有立即刷新时，可让用户关闭并重新打开 `Win+N`；不要擅自重启 Explorer。

## 数据与权限

正常待办读写只访问本机 Windhawk 的 `ModsWritable/<mod-id>/LocalStorage`。安装阶段可能涉及 winget 安装 Python/Windhawk、Windhawk 本地 mod 编译、HKLM 下的 Windhawk 存储写入以及 UAC 提升；这些动作都必须遵守宿主的用户授权流程。

存储格式见 `references/storage-format.md`。
