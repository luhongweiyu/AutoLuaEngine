# 0014：Android Lua 运行时模块使用 require 加载

- 状态：已接受
- 日期：2026-08-03

## 背景

Android Worker 原先由 Java 把 `api_m.lua`、兼容层、LuaSocket 模块注册代码和 bootstrap 拼成
运行时字符串，C++ 再追加文本用户源码并由 Lua 一次加载。Lua 文件边界因此不形成词法边界：运行时文件的
顶层 `local` 可能遮蔽后续用户代码中的同名全局 API；语法和运行错误也无法准确定位到原 asset。
ALPKG 已经先执行 runtime、再单独加载入口字节码，文本脚本与它存在不必要的加载差异。

## 决定

1. Java 在 Worker 初始化时只把当前 APK 的 `AssetManager` 交给 native。模块白名单和 asset 路径
   固定在引擎侧，由 native 一次性读取源码并深拷贝到 `Engine`；Root 与非 Root Worker 使用同一
   配置和入口，Java 不参与 Lua 模块依赖或加载顺序。
2. 每个 Lua VM 将各 asset 以 `@runtime/...` 真实 chunk 名独立编译为 loader，并注册到
   `package.preload`。不拼接不同 Lua 文件，也不把 runtime 与用户源码拼成同一 chunk。
3. `bootstrap.lua` 是唯一初始化入口，通过 `require` 声明 `api_m`、扩展兼容层和 `lr/cd` 的依赖；
   各模块返回自己的表，`bootstrap.lua` 集中发布 `_G.m`、`_G.lr`、`_G.cd` 和默认一级全局 API。
4. 文本脚本与 ALPKG 共用同一顺序：根状态完成 runtime `require`，再在共享全局环境的主任务
   子状态加载用户文本或包入口。LuaSocket 继续使用标准模块名和 `package.preload`。

## 后果

- 每个 runtime 文件的 `local` 天然限制在所属 chunk 内，由已导出函数闭包按需保留。
- `require` 缓存保证共享模块只初始化一次，依赖关系由 Lua 文件本身表达，不再依赖 Java 拼接顺序。
- 用户文本的裸全局调用只会命中 bootstrap 导出的公开 API；错误栈可显示真实 runtime 文件名。
- 新增 runtime Lua 模块时必须注册命名 asset、显式 `require` 依赖并返回模块值，不能恢复源码拼接。
