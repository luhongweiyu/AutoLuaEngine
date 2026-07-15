---
params: "packageName: string, componentName: string?, isOpenBySuper: boolean?"
returns: "无"
---

启动应用；有组件名时精确启动，否则打开启动入口。`isOpenBySuper` 为兼容参数，当前 Root
引擎始终以最高权限执行，因此传入值不改变行为。
