# 小鱼精灵文档总入口

这里同时服务后续开发、AI 接手和用户发布。首要目标是保存当前项目的真实边界和决策；公开
文档从明确的发布区提供，不能反过来替代内部权威资料。

## 目录职责

| 目录 | 用途 | 是否可直接发布 |
|---|---|---|
| [`internal/`](internal/) | 当前计划、架构、构建方式、契约和平台实现说明 | 否 |
| [`public/`](public/) | 面向用户的宣传首页、API 文档及静态网页入口 | 是 |
| [`archive/`](archive/) | 历史核对与旧项目参考，只用于追溯 | 否 |

只有 `public/` 是发布白名单。以后制作官网或同步到静态托管时，应以该目录为根目录，不要
复制整个 `docs/`。

## AI 接手顺序

1. 阅读仓库根目录的 [`AGENTS.md`](../AGENTS.md)。
2. 阅读 [项目总计划](internal/项目总计划.md)，确认当前阶段和已完成范围。
3. 阅读 [架构设计](internal/架构设计.md)，理解进程、语言绑定、设备能力和 IDE 工具边界。
4. 按任务类型阅读下面的权威资料，不必无差别通读全部文件。
5. 最后检查目标模块的源码、测试和局部 `README.md`；代码与测试是核实现状的最终证据。

重要结论必须沉淀到仓库文档，不要求后续 AI 获得历史聊天记录。

## 按任务查文档

| 任务 | 必读文档 |
|---|---|
| 全局架构、能力规划 | [项目总计划](internal/项目总计划.md)、[架构设计](internal/架构设计.md)、[AI 执行指南](internal/AI_执行指南.md) |
| C ABI、脚本 API 分层 | [统一 API 契约](internal/contracts/API_契约.md) |
| 插件 SO | [插件 SO API](internal/contracts/插件_SO_API.md) |
| Android 引擎、Root、截图、Java、UI、ImGui、多线程 | [`internal/platform/android/`](internal/platform/android/) 中对应说明 |
| IDE / 抓图取色器 | [工具说明](internal/ide/PC_抓图取色工具.md)、[行为契约](internal/contracts/PC_抓图取色器_行为契约.md) |
| 构建或运行 | [构建与运行](internal/构建与运行.md) |
| 官网与脚本 API 用法 | [宣传首页](public/index.html)、[公开脚本文档](public/脚本文档.md) 或 [交互式脚本文档](public/脚本文档.html) |
| 当前需要用户配合的事项 | [用户待办事项](internal/用户待办事项.md) |
| 历史原因 | [归档说明](archive/README.md)，只用于追溯 |

## 权威关系

- `internal/contracts/` 定义必须保持稳定的行为和接口。
- `internal/platform/` 说明这些契约当前如何实现，但不能擅自扩大公开能力。
- `public/` 只解释用户可以依赖的用法；它必须来自已实现能力。
- `archive/` 不再维护为当前事实。
- 如果文档与代码不一致，先用源码和测试确认实际行为，再同时修正相应契约与用户文档，
  不要只改其中一份来掩盖冲突。

## 更新清单

- 脚本 API 变化：更新 `internal/contracts/API_契约.md`、公开函数页、公开 API 总览和
  `public/脚本文档/catalog.json`，然后运行 `tools/检查脚本文档.ps1`。
- IDE 工具行为变化：更新行为契约和工具说明；已完成的阶段性核对不追加到归档清单中。
- 架构或长期维护决定：在 `internal/decisions/` 新增决策记录，并更新受影响的现行文档。
- 仅排版或位置调整：不要把它描述成新的业务能力。
- 发布文档：只发布 `public/`，并保持其内部相对路径不变。

长期决定见 [项目决策记录](internal/decisions/README.md)。
