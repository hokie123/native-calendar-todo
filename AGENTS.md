# Agent installation guide

当用户把这个仓库交给 AI Agent，并要求“安装这个 Skill”“让我以后能直接操作 Windows 日历待办”时，按以下规则执行。

## 目标

只安装此目录作为 Skill：

```text
skills/native-calendar-todo
```

不要把整个仓库根目录误当作 Skill。标准入口是：

```text
https://github.com/hokie123/native-calendar-todo/tree/main/skills/native-calendar-todo
```

## 安装策略

1. 先读取 `skills/native-calendar-todo/SKILL.md` 并做宿主要求的安全检查。
2. 如果宿主有原生 Agent Skills / GitHub Skill Installer，优先使用原生安装器。
3. 如果宿主只能从本地导入，下载或复制 `skills/native-calendar-todo` 目录，再导入到宿主的用户级或项目级 skills 目录。
4. 涉及本地脚本执行、文件写入、注册表写入或提升权限时，遵守宿主权限模型；需要用户确认就请求确认，不要绕过。
5. 安装后运行：

```powershell
python scripts/calctl.py doctor --json
```

只有检查通过后，才能告诉用户该 Skill 已具备实际读写能力。

## 运行时前提

- Windows 11
- Python 3
- 已安装并启用 Native Calendar Todo Windhawk mod
- 标准 Windhawk 注册表本地存储可访问

Skill 本身不会自动安装 Windhawk，也不会自动把 `native-calendar-todo.wh.cpp` 编译进 Windows Shell。若用户要求同时安装 mod，应明确说明这是第二个部署步骤，并在执行任何系统级安装/修改前遵守宿主的授权流程。

## 权限边界

正常运行只需执行本地 `scripts/calctl.py`，并读取/写入 Windhawk 的：

```text
HKLM\SOFTWARE\Windhawk\Engine\ModsWritable\<mod-id>\LocalStorage
```

不需要把待办内容上传到网络。不要引入不存在的 companion service、named pipe、SQLite 或提醒服务。

## 成功标准

安装成功 ≠ 可用。必须满足：

```text
Skill 已被宿主识别
+ calctl.py doctor 通过
+ 至少能成功 list
```

如果 `doctor` 找不到 Windhawk mod，只报告缺少运行时前提，不要伪造完成状态。
