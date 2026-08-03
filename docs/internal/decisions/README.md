# 项目决策记录

这里记录会长期影响架构、维护方式或发布边界的决定。每份记录至少包含状态、日期、背景、
决定和后果。

- 已接受的决定发生变化时，新增后续记录并标明替代关系，不静默重写历史。
- 临时实现细节和普通待办不放在这里。
- 聊天记录可以帮助讨论，但不能代替最终决策文档。

## 已接受

- [0001：内部权威资料与公开发布边界](0001-内部权威资料与公开发布边界.md)
- [0002：官网静态发布与下载边界](0002-官网静态发布与下载边界.md)
- [0003：Android 脚本 API 兼容性设计基线](0003-Android脚本API兼容性设计基线.md)
- [0004：Android 系统接口历史参考顺序](0004-Android系统接口历史参考顺序.md)
- [0005：Android 项目固定签名](0005-Android项目固定签名.md)
- [0006：Android Lua 公开层与内部实现边界](<0006-Android Lua公开层与内部实现边界.md>)
- [0007：Android LuaSocket 上游接入](<0007-Android LuaSocket上游接入.md>)
- [0008：Android Lua CFFI 上游接入](<0008-Android Lua CFFI上游接入.md>)（ABI 发布与验收范围由
  [0015](0015-Android第一版ABI支持范围.md)更新）
- [0009：ALPKG 小资源范围与 YOLO 模型分离](0009-ALPKG小资源范围与YOLO模型分离.md)
- [0010：Android 可选 YOLO 运行时与多语言 C ABI](0010-Android可选YOLO运行时与多语言C-ABI.md)
- [0011：Android 本地扩展文件导入与按需加载](0011-Android本地扩展文件导入与按需加载.md)
- [0012：Android 扩展目录包与相对路径](0012-Android扩展目录包与相对路径.md)
- [0013：Android 一次性脚本 Worker 与 Root 权限边界](0013-Android一次性脚本Worker与Root权限边界.md)
- [0014：Android Lua 运行时模块使用 require 加载](<0014-Android Lua运行时模块使用require加载.md>)
- [0015：Android 第一版 ABI 支持范围](0015-Android第一版ABI支持范围.md)
