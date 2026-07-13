/**
 * 文件用途：声明引擎主对象，是 Java/JNI 调用 native 核心能力的统一入口。
 */
#pragma once

#include <string>
#include <memory>
#include <atomic>
#include <condition_variable>
#include <mutex>

class AlpkgPackage;
class LuaRuntime;

/**
 * Native 引擎总入口。
 *
 * JNI 层只和 Engine 交互，避免 JNI 文件直接依赖具体脚本语言运行时。
 * 后续加入 ScriptTask、JSRuntime、任务状态时，会优先扩展这个类。
 */
class Engine {
public:
    Engine();
    ~Engine();

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    /**
     * 初始化引擎。
     *
     * 当前只记录初始化状态；后续会在这里挂载平台能力、日志通道和任务管理器。
     */
    void init();

    /**
     * 执行 Lua 文本。
     *
     * @param code Lua 源码文本。
     * @return 执行结果摘要。
     */
    std::string runLuaText(const char* code);

    /** 运行已经由 Java 校验路径并由 native 打开的 ALPKG 脚本包。 */
    std::string runLuaPackage(
            const std::shared_ptr<AlpkgPackage>& package,
            const char* runtimeBootstrap
    );

    /**
     * 查询任务状态。
     *
     * @param taskId 指定任务 ID；传 0 表示查询当前或最后一个任务。
     * @return JSON 字符串，供 Java HTTP 层直接转换成 JSONObject。
     */
    std::string statusJson(int taskId) const;

    /**
     * 请求停止当前脚本。
     *
     * 当前使用 Lua debug hook 协作取消。不会强杀线程。
     *
     * @return true 表示存在运行、暂停或等待暂停的脚本，并已接受停止请求；false 表示
     * 当前没有可停止脚本，或脚本已经处于 stopping 状态。
     */
    bool requestStop();

    /**
     * 请求暂停当前脚本。
     *
     * 暂停是协作式的：Lua VM 执行到 debug hook 后进入等待，不直接挂起系统线程。
     */
    bool requestPause();

    /**
     * 请求恢复已暂停或正在等待暂停的脚本。
     */
    bool requestResume();

private:
    bool initialized_;
    int nextTaskId_;
    std::atomic_bool stopRequested_;
    std::atomic_bool pauseRequested_;
    std::condition_variable controlCondition_;
    std::mutex controlMutex_;
    std::mutex runMutex_;
    std::mutex runtimeMutex_;
    mutable std::mutex taskMutex_;
    LuaRuntime* activeLuaRuntime_;
    int lastTaskId_;
    std::string lastStatus_;
    std::string lastResult_;
    std::string lastError_;

    std::string runLuaInternal(
            const std::shared_ptr<AlpkgPackage>& package,
            const char* code,
            const char* runtimeBootstrap
    );

    static bool shouldInterrupt(void* context);
    bool waitIfPausedOrStopped();
    bool isActiveStatusLocked() const;
};
