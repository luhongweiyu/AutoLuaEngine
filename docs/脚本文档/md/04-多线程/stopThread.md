---
params: "无"
returns: "无"
---

停止并等待指定子线程退出。

`thread:stopThread()` 设置子任务停止标记，并在释放调用者 Gate 后等待目标线程退出。
目标任务会在指令 hook、`sleep` 或 UI 等待中收到停止请求。禁止使用 `pthread_cancel`，
避免截断 Lua 栈、JNI 引用和 C++ 对象析构。

自然结束的子线程会释放对应 Lua registry 引用；下一次创建线程时回收已经结束的
`std::thread` 句柄。主脚本结束时执行最终全量停止和 join。
