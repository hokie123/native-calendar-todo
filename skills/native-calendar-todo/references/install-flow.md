# 自动安装 / 检测流程

这个 Skill 的安装分为两个层级：

1. **Agent Skill 本身**：由 WorkBuddy、Codex、CodeBuddy 等宿主安装。
2. **Windows 运行时**：Windhawk + Native Calendar Todo mod + Python 3。

`scripts/bootstrap.ps1` 负责第二层，并以机器可读的 JSON 返回当前状态。

## Agent 推荐流程

先只检测，不修改系统：

~~~powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/bootstrap.ps1 -Check -Json
~~~

如果返回的 `ok` 为 `false`，根据 `next_action` 处理：

| next_action | 含义 | Agent 应做什么 |
|---|---|---|
| `install_python` | 缺少 Python 3 | 获得用户授权后运行 `-Apply -Yes` |
| `install_windhawk` | 缺少 Windhawk | 获得用户授权后运行 `-Apply -Yes` |
| `install_windhawk_development_tools` | 有 Windhawk 2 CLI，但不能本地编译 mod | 打开 Windhawk 安装 Development Tools，再重跑 |
| `windhawk_ui_install` | 当前 Windhawk 没有 CLI，例如 1.7.3 | 如果宿主支持 GUI 自动化，自动完成 Create a new mod → 粘贴 → Compile Mod；否则让用户完成这一小步 |
| `fix_doctor` | 组件都在，但最终验证失败 | 根据 `doctor.stderr` 排查 |
| `ready` | 全部可用 | 可以开始调用待办 |

获得用户明确授权后：

~~~powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/bootstrap.ps1 -Apply -Yes -LaunchWindhawk -Json
~~~

`-Yes` 的含义是“宿主已经取得用户对这些系统修改的明确授权”，不是用来绕过授权。

## 为什么分 CLI 和 UI 两条路径

Windhawk 2.0 提供 `windhawk-cli.exe`，可以从命令行安装本地 mod：

~~~powershell
windhawk-cli.exe mod install --file <source.wh.cpp> --yes
~~~

本地文件安装会进行本地编译，所以还需要 Windhawk Development Tools。

Windhawk 1.7.3 稳定版没有 `windhawk-cli.exe`。bootstrap 会把完整 mod 源码复制到剪贴板，并可启动 Windhawk；支持桌面自动化的 Agent 可以继续点击完成安装。

## Skill 宿主

优先使用宿主自己的 Skill 安装器和权限模型。

bootstrap 也支持两个有公开目录约定的宿主：

~~~powershell
# Codex 用户级
powershell -File scripts/bootstrap.ps1 -Apply -Yes -Agent Codex -SkillScope User

# CodeBuddy 用户级
powershell -File scripts/bootstrap.ps1 -Apply -Yes -Agent CodeBuddy -SkillScope User
~~~

当前公开约定：

- Codex 用户级：`~/.agents/skills/native-calendar-todo`
- Codex 项目级：`.agents/skills/native-calendar-todo`
- CodeBuddy 用户级：`~/.codebuddy/skills/native-calendar-todo`
- CodeBuddy 项目级：`.codebuddy/skills/native-calendar-todo`

WorkBuddy 应优先走其原生 Upload / Find Skills 安装流程，不猜测内部配置目录。需要指定目录时可使用 `-SkillDestination`。

## 成功标准

只有以下条件全部满足才算“安装完成”：

~~~text
Windows 11
+ Python 3 可执行
+ Windhawk 可用
+ Native Calendar Todo mod 已安装
+ calctl.py doctor 成功
+ calctl.py list 成功
~~~

不要只因为 Skill 文件已经复制，就告诉用户“安装完成”。
