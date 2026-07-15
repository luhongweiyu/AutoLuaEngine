---
params: "id: integer, x: integer, y: integer"
returns: "无"
---

按住不放，仅 Root 模式。

```lua
touchDown(0, 100, 100)
sleep(50)
local ok = touchMove(0, 200, 200)
touchUp(0)
print(ok)
```

参数：

- `id:integer`：模拟手指索引，范围 `0` 到 `4`。
- `x:integer, y:integer`：屏幕坐标。

返回：

- `touchDown`：无返回值。
- `touchMove`：返回 `boolean`，表示命令是否已执行。
- `touchUp`：无返回值。


输入注入当前只走 Root helper 常驻进程，不走无障碍，也不为每个命令拉起外部
`input` 进程。Root 不可用时返回 `false`，`touchDown` / `touchUp` 按兼容语义不返回值。
