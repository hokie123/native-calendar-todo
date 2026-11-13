---
name: native-calendar-todo
description: 让本地 AI Agent 读取和管理 Windows 11 原生日历弹窗中的 Native Calendar Todo 待办，包括查询、新增、修改、完成、恢复和删除。用户提到“日历待办”“Windows 待办”“帮我记到日历”“查看/修改/完成待办”或希望由本地 Agent 操作该 Windhawk 日历待办时使用。仅适用于已安装并启用 Native Calendar Todo Windhawk mod 的 Windows 11。
license: GPL-3.0-only
compatibility: Windows 11；需要 Python 3，以及已安装并启用的 Native Calendar Todo Windhawk mod；使用标准 Windhawk 注册表本地存储。Windhawk Portable 暂不保证兼容。
metadata:
  author: hokie123
  version: "0.1.0"
  repository: "https://github.com/hokie123/native-calendar-todo"
---

# Native Calendar Todo

通过 `scripts/calctl.py` 直接管理 Windows 11 日历弹窗中的 Native Calendar Todo 待办。脚本只使用 Python 标准库，直接读写 Windhawk 的本地存储，不依赖额外后台服务、数据库或云端同步。

## 使用前检查

先运行：

```powershell
python scripts/calctl.py doctor --json
```

只有 `ok: true` 才继续。若 `writable: false`，读操作仍可能可用；执行新增、修改、完成或删除前，应让宿主请求用户授予所需的本地写入/提升权限。不要绕过宿主的安全确认。

如果提示找不到 Windhawk 存储，说明 Native Calendar Todo mod 尚未安装、未启用，或使用了不同的 mod ID。不要假装已经完成写入。

## 常用操作

```powershell
# 查看全部待办
python scripts/calctl.py list --json

# 查看某一天 / 仅查看未完成
python scripts/calctl.py list --day 20261115 --json
python scripts/calctl.py list --pending --json

# 查看可用分类
python scripts/calctl.py categories --json

# 新增
python scripts/calctl.py add --title "给老师回邮件" --day 20261115 --time 09:30 --category work --json

# 修改：--match 必须唯一；必要时用 --on 限定原日期
python scripts/calctl.py edit --match "给老师回邮件" --on 20261115 --day 20261116 --time 10:00 --json

# 完成 / 恢复
python scripts/calctl.py done --match "给老师回邮件" --json
python scripts/calctl.py done --match "给老师回邮件" --undo --json

# 删除
python scripts/calctl.py rm --match "给老师回邮件" --json
```

## Agent 行为规则

1. 把“今天、明天、下周一”等相对日期解析为本机日期后，再转换成 `YYYYMMDD`；执行前向用户明确最终日期，尤其是在跨午夜附近。
2. 新增待办必须显式传入 `--day`。不要因为用户没有说日期就默认写到今天。
3. 写操作前先读取相关待办；`--match` 必须唯一。匹配多条时，不要猜测目标。
4. `category` 应来自 `categories --json` 返回的 ID。默认分类为 `work`、`important`、`personal`，但用户可能已自定义。
5. 每次写操作后，以命令返回值和重新读取结果为准；失败时直接报告错误，不要声称已完成。
6. 不要批量覆盖、清空或重写整个列表，除非用户明确要求。脚本包含并发变化检测，用于避免覆盖用户刚刚在日历 UI 中做出的修改。
7. 该 mod 当前没有 Agent 侧提醒/通知能力；不要承诺 `remind`、后台推送或云同步。
8. 如果写入成功但日历弹窗尚未刷新，可让用户关闭并重新打开 `Win+N`。不要为刷新而强制重启 Explorer 或 Windhawk，除非用户明确同意。

## 数据与权限

正常使用只访问本机 Windhawk 的 `ModsWritable/<mod-id>/LocalStorage`，不会把待办内容发送到网络。脚本可能需要读取或写入 `HKLM` 下的 Windhawk 本地存储；由宿主根据自身权限模型向用户请求授权。

存储格式与兼容性细节见 `references/storage-format.md`。
