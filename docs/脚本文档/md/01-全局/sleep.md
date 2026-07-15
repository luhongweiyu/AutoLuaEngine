---
params: "ms: integer"
returns: "boolean"
---

脚本延时，成功返回 `true`。

也可用 `m.sleep(ms)`，参数与返回值一致。

在多线程场景下，`sleep` 会在等待期间释放 VM Gate，返回前重新获取。
