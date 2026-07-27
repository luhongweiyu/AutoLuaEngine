# 小鱼精灵公开文档

宣传首页按长期产品范围介绍 JavaScript、Lua、Go 与 PC、Android、iOS。当前实现仍以
Android + Lua 为第一版目标；这里的脚本文档只收录已经实现、脚本用户可以直接依赖的
API，具体可用性以脚本文档和 GitHub Releases 为准。

## 阅读入口

- [小鱼精灵官网](index.html)：IDE 工具、JavaScript/Lua/Go 与 PC/Android/iOS 产品介绍。
- [交互式脚本文档](脚本文档.html)：目录、搜索、参数摘要和函数正文集中在一个页面中。
- [Markdown 脚本文档](脚本文档.md)：适合在 Git 仓库中阅读和链接。

建议先阅读“概述”中的 API 总览、命名空间和类型约定，再进入对应功能分类查找函数。

## 发布说明

本目录是文档发布白名单。部署时以 `docs/public/` 为站点根目录，并保留以下相对结构：

```text
index.html
assets/
脚本文档.html
脚本文档/
  catalog.json
  md/
```

宣传首页是独立的纯静态 HTML；脚本文档会按需读取目录 JSON 和 Markdown 正文，因此发布时
仍不能只复制 HTML 文件。内部架构、源码说明、计划和历史记录不属于公开站点。

官网品牌主图保存在 `assets/xiaoyv-brand-master.png`。网页展示图、社交分享图和图标都放在
同一目录；调整展示尺寸时应保留主图，不要反复压缩后覆盖源文件。

## 反馈问题

反馈脚本 API 问题时，请同时提供：

- 使用的函数名和最小复现脚本。
- 实际参数、返回值或完整错误信息。
- Android 版本、是否启用 Root，以及普通脚本或 `.alpkg` 运行方式。
- 预期结果和实际结果。
