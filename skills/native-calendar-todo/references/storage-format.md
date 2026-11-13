# Native Calendar Todo 存储格式

当前 Windhawk mod 使用 `Wh_GetStringValue` / `Wh_SetStringValue` 保存待办。

## 注册表位置

标准 Windhawk 安装下，本地存储位于：

```text
HKLM\SOFTWARE\Windhawk\Engine\ModsWritable\<mod-id>\LocalStorage
```

本项目常见 mod ID：

```text
local@win11-native-todo-calendar
win11-native-todo-calendar
```

`calctl.py` 会优先检查这两个 ID，并尝试发现以 `win11-native-todo-calendar` 结尾的可用项。也可用环境变量 `CALCTL_MOD_ID` 指定实际 ID。

## 待办值

当前值名：`todo-tasks-v2`。

首行为：

```text
#v2
```

之后每行 8 个 Tab 分隔字段：

```text
day    done    categoryId    title    time    location    url    notes
```

- `day`：`YYYYMMDD`
- `done`：`0` 或 `1`
- `time`：空、`HH:MM` 或 `HH:MM-HH:MM`
- 文本字段对 `%`、Tab、LF、CR 使用 `%25`、`%09`、`%0A`、`%0D` 转义
- 总字符串受 Windhawk 当前缓冲区限制，mod 侧拒绝约 65535 UTF-16 code units 以上的数据

旧值 `todo-tasks-v1` 仅用于兼容读取；`calctl.py` 在首次成功写入时会自然升级为 v2。

## 分类值

值名：`todo-categories-v2`。每行 3 个 Tab 分隔字段：

```text
id    name    #RRGGBB
```

如果没有自定义分类，mod 默认使用：

```text
work       工作    #2563EB
important  重要    #EA580C
personal   个人    #059669
```

## 并发保护

Agent 的写操作采用“读取 → 修改 → 写入”的方式。`calctl.py` 在写入前会再次检查 `todo-tasks-v2` 是否仍与最初读取值一致；如果用户或另一个客户端在此期间修改了列表，脚本会停止并要求重新读取，避免静默覆盖。
