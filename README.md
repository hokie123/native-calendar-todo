# Native Calendar Todo for Windows 11

在 **Windows 11 原生日历 / 通知中心弹窗**中加入可编辑待办，并标记中国法定节假日与调休工作日。项目基于 Windhawk，不替换系统日历界面。

> 当前 mod 版本：`v0.4.9`。Agent Skill 版本：`0.2.0`。

![功能概览：空待办、创建待办、分类颜色管理、保存后的日历待办](feature-overview.png)

## 功能

- 按日期创建、编辑、完成和删除待办
- 支持时间、地点、会议链接、备注、自定义分类与颜色
- 在日期格中显示待办密度提示
- 通过 [CN-Holidays](https://github.com/cfap/CN-Holidays) 标记中国法定节假日和调休工作日
- 待办保存在 Windhawk 本机存储中
- 提供可被本地 AI Agent 调用的标准 `SKILL.md`
- 提供 bootstrap：自动检测 Windows、Python、Windhawk、mod、权限和最终可用性

当前不包含：农历/传统节日/节气、云同步、跨设备同步、独立桌面程序，以及 Agent 侧提醒推送。

## 最简单的安装方式：把仓库交给 AI Agent

可以直接把这句话发给 WorkBuddy、Codex、CodeBuddy 或其他支持 Agent Skills / 本地脚本的 Agent：

> 安装这个项目并让我以后可以直接操作 Windows 11 日历待办：https://github.com/hokie123/native-calendar-todo 。先读取 AGENTS.md 和 skills/native-calendar-todo/SKILL.md，先做只读检测；需要安装依赖、修改系统或提升权限时向我请求授权。获得授权后按 bootstrap 流程继续，最后必须通过 doctor 和 list 验证后才能告诉我安装完成。

### Agent 实际执行的流程

第一步只检测，不修改系统：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File install.ps1 -Check -Json
```

获得用户明确授权后：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File install.ps1 -Apply -Yes -LaunchWindhawk -Json
```

bootstrap 会依次检测：

```text
Windows 11
→ Python 3
→ Windhawk
→ Windhawk CLI / Development Tools
→ Native Calendar Todo mod
→ calctl doctor
→ calctl list
```

结果会返回机器可读的 `next_action`。详细状态机见 [`install-flow.md`](skills/native-calendar-todo/references/install-flow.md)。

## 自动化能做到什么程度

### 有 Windhawk CLI + Development Tools

这是最完整的自动化路径。bootstrap 可以直接从 Skill 自带的 `.wh.cpp` 源码编译并安装 mod。标准安装需要管理员写入时，Windows 可能显示 UAC；用户点允许后继续。

### 当前 Windhawk 没有可用 CLI

bootstrap 会：

1. 自动检测到这个情况。
2. 把完整 mod 源码复制到剪贴板。
3. 可自动打开 Windhawk。
4. 返回 `next_action: windhawk_ui_install`。

如果 WorkBuddy 等宿主具有桌面/GUI 自动化能力，它可以继续替用户点击 **Create a new mod → 粘贴 → Compile Mod → 启用**。只有终端能力的 Agent 则应明确让用户完成这一小步，然后重新检测。

## Skill 是自包含的

真正的 Skill 目录：

```text
skills/native-calendar-todo
```

其中已经包含：

```text
SKILL.md
scripts/
  bootstrap.ps1
  calctl.py
assets/
  win11-native-todo-calendar.wh.cpp
references/
  install-flow.md
  storage-format.md
```

因此 WorkBuddy 等工具只导入 Skill 文件夹，也有足够文件完成环境检测和 mod 部署，不依赖仓库根目录。

## 不同 Agent

### WorkBuddy

优先使用 WorkBuddy 自身的 Upload Skill / Find Skills。它应先做安全检查；是否继续自动安装由其安全结果和权限策略决定。

### Codex

常用目录：

- 用户级：`~/.agents/skills/`
- 项目级：`.agents/skills/`

bootstrap 可选直接复制：

```powershell
powershell -File scripts/bootstrap.ps1 -Apply -Yes -Agent Codex -SkillScope User
```

### CodeBuddy

- 用户级：`~/.codebuddy/skills/`
- 项目级：`.codebuddy/skills/`

bootstrap 可选直接复制：

```powershell
powershell -File scripts/bootstrap.ps1 -Apply -Yes -Agent CodeBuddy -SkillScope User
```

### 其他 Agent

只要支持 Agent Skills / `SKILL.md` 和本地脚本执行，就可以导入整个 `skills/native-calendar-todo` 目录。未知宿主不要猜安装路径，优先使用宿主自己的导入机制。

## 手工安装 Windhawk mod

不使用 Agent 时仍然可以手工安装：

1. 安装 [Windhawk](https://windhawk.net/)。
2. 在 Windhawk 中选择 **Create a new mod**。
3. 打开 [`native-calendar-todo.wh.cpp`](native-calendar-todo.wh.cpp)，复制完整源码。
4. 粘贴到 Windhawk mod 编辑器。
5. 点击 **Compile Mod** 或按 `Ctrl+B`，编译成功后启用。
6. 按 `Win+N` 检查。

升级已有安装时，建议保留相同 mod ID `win11-native-todo-calendar`，以兼容已有待办数据。

## Agent 如何操作待办

Skill 内置 [`calctl.py`](skills/native-calendar-todo/scripts/calctl.py)，直接读写 Windhawk 的本地待办存储。

```powershell
python skills\native-calendar-todo\scripts\calctl.py doctor --json
python skills\native-calendar-todo\scripts\calctl.py list --json
python skills\native-calendar-todo\scripts\calctl.py add --title "给老师回邮件" --day 20261115 --time 09:30 --json
```

详细行为规则见 [`SKILL.md`](skills/native-calendar-todo/SKILL.md)。

## 数据与隐私

待办内容保存在本机 Windhawk 存储中。正常使用不需要把待办上传到网络。

安装阶段可能需要：

- winget 下载 Python 或 Windhawk
- Windhawk 本地编译
- HKLM 下的 Windhawk 配置/本地存储
- UAC 提升

所有这些步骤都应由 Agent 宿主按照自己的权限模型向用户请求授权。

## 已知限制

- Windows 功能更新可能改变 Shell/XAML 结构，需要调整 Hook。
- 节假日 ICS 来自第三方来源，Shell 重启后首次加载可能需要网络。
- 没有 CLI 的 Windhawk 版本，本地 mod 安装需要 UI 或 GUI 自动化。
- 有 CLI 时，本地源码安装仍需要 Windhawk Development Tools。
- Windhawk Portable 的数据位置不同，当前待办客户端主要面向标准注册表存储。

## 开发与验证

Skill 内的 Windhawk 源码副本必须与根目录 [`native-calendar-todo.wh.cpp`](native-calendar-todo.wh.cpp) 完全一致。GitHub Actions 会检查：

- PowerShell bootstrap 能否通过语法解析
- `calctl.py` 能否编译
- Skill 内打包的 `.wh.cpp` 是否与根目录同步

## 来源与许可证

本项目基于 `lonfro` 的 **[Agenda in Calendar View](https://windhawk.net/mods/agenda-in-calendar-view)**，其中 Shell hooking 工作亦保留对 `m417z` 的原始署名。仓库继续保留上游版权声明，并按 **GNU GPL v3** 发布，详见 [`LICENSE`](LICENSE)。
