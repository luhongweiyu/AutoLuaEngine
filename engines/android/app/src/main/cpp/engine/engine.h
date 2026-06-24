#pragma once

#include <string>
#include <atomic>
#include <mutex>

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
     */
    void requestStop();

private:
    bool initialized_;
    int nextTaskId_;
    std::atomic_bool stopRequested_;
    mutable std::mutex taskMutex_;
    int lastTaskId_;
    std::string lastStatus_;
    std::string lastResult_;
    std::string lastError_;

    static bool isStopRequested(void* context);
};
