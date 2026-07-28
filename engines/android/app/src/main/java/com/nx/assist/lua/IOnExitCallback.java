/**
 * 文件用途：定义 LuaEngine.registerExitCallback 的单方法 Lua/Java 兼容回调。
 */
package com.nx.assist.lua;

/** 脚本结束时接收异常标记和结束码。 */
public interface IOnExitCallback {
    void onExit(boolean error, int exitCode);
}
