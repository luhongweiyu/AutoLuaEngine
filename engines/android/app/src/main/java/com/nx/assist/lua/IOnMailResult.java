/**
 * 文件用途：兼容懒人精灵 LuaEngine 邮件发送结果回调。
 */
package com.nx.assist.lua;

public interface IOnMailResult {
    void onFailed(String message);

    void onSuccess();
}
