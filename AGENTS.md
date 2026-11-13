# Agent installation guide

当用户把这个仓库交给 AI Agent，并说“安装这个 Skill”“把 Windows 日历待办装好”“以后直接帮我管理日历待办”时，按下面的状态机执行。

## 目标

真正的 Skill 包是：

```text
skills/native-calendar-todo
```

它已经是自包含包，内含 `SKILL.md`、bootstrap、待办客户端和 Windhawk mod 源码。优先使用宿主原生的 Skill 安装机制，不要把整个仓库根目录当成 Skill 包。

## 安装原则

1. 先读取 `skills/native-calendar-todo/SKILL.md`。
2. 先做宿主安全扫描。
3. 系统修改前取得用户明确授权。
4. 能自动完成的步骤自动完成；不能可靠自动化的步骤返回精确的 `next_action`。
5. 最后必须通过 `doctor + list` 验证。
6. 不要因为文件已经复制就宣称“安装完成”。

## 第一步：只检测

从 Skill 目录执行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/bootstrap.ps1 -Check -Json
```

如果是仓库根目录，也可以执行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File install.ps1 -Check -Json
```

## 第二步：获得授权后应用

如果用户已经批准安装缺失依赖和本地 mod：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/bootstrap.ps1 -Apply -Yes -LaunchWindhawk -Json
```

`-Yes` 表示“授权已经由宿主取得”。不要未经用户批准自行添加。

## next_action 处理

### ready

安装完成。随后可以直接调用 `calctl.py`。

### install_python

缺少 Python。bootstrap 会通过 winget 尝试安装可用的 Python 3。

### install_windhawk

缺少 Windhawk。bootstrap 使用官方 winget 包 `RamenSoftware.Windhawk` 安装稳定版。

### install_windhawk_development_tools

机器上有 Windhawk CLI，但没有本地编译工具。如果宿主支持桌面自动化，打开 Windhawk、安装 Development Tools、再重跑 bootstrap；否则明确告诉用户只需完成这一步。

### windhawk_ui_install

表示当前 Windhawk 没有可用于自动安装本地 mod 的 CLI。bootstrap 已经把 `assets/win11-native-todo-calendar.wh.cpp` 的完整内容复制到剪贴板，并可打开 Windhawk。

如果宿主支持桌面自动化，继续完成：

1. Create a new mod。
2. 全选示例代码。
3. 粘贴剪贴板源码。
4. Compile Mod。
5. 启用 mod。
6. 回到终端重跑 `bootstrap.ps1 -Check -Json`。

如果宿主没有 GUI/Computer Use 能力，不要伪装自动完成；让用户执行这些点击步骤即可。

## Windhawk CLI 路径

当 `windhawk-cli.exe` 与 Development Tools 都存在时，bootstrap 会直接调用：

```powershell
windhawk-cli.exe mod install --file assets/win11-native-todo-calendar.wh.cpp --yes
```

标准安装可能触发 UAC。用户点允许后继续。本地文件安装的 storage ID 会使用 `local@win11-native-todo-calendar`，Skill 的存储检测同时兼容本地和普通 ID。

## 宿主 Skill 安装

### Codex

用户级：`~/.agents/skills/native-calendar-todo`

项目级：`.agents/skills/native-calendar-todo`

bootstrap 可选：

```powershell
powershell -File scripts/bootstrap.ps1 -Apply -Yes -Agent Codex -SkillScope User
```

### CodeBuddy

用户级：`~/.codebuddy/skills/native-calendar-todo`

项目级：`.codebuddy/skills/native-calendar-todo`

bootstrap 可选：

```powershell
powershell -File scripts/bootstrap.ps1 -Apply -Yes -Agent CodeBuddy -SkillScope User
```

### WorkBuddy

优先使用 WorkBuddy 自己的 Upload Skill / Find Skills 机制，让它执行安全检查并决定是否需要人工确认。不要猜测内部配置目录。

### 其他兼容 Agent Skills 的宿主

让宿主按其标准把 `skills/native-calendar-todo` 整个目录导入。需要自定义目录时，bootstrap 支持 `-SkillDestination <path>`。

## 成功标准

只有下面全部成立才算真正完成：

```text
Skill 被宿主识别
+ Windows 11
+ Python 3 可执行
+ Windhawk 可用
+ Native Calendar Todo mod 已安装
+ calctl.py doctor 成功
+ calctl.py list 成功
```

失败时返回实际状态，不要猜测。
