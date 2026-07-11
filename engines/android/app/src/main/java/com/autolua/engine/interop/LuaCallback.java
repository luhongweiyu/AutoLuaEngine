/**
 * 文件用途：保存 Lua 函数注册表引用，使 Java 动态代理可以回调所属 LuaRuntime。
 */
package com.autolua.engine.interop;

/**
 * Lua 函数回调句柄。
 *
 * runtimeToken 用于定位仍然存活的 LuaRuntime，reference 是该函数在 Lua registry
 * 中的引用。引用统一由 LuaRuntime 销毁时释放，避免依赖 Java GC 的执行时机。
 */
public final class LuaCallback {
    private final long runtimeToken;
    private final int reference;

    public LuaCallback(long runtimeToken, int reference) {
        this.runtimeToken = runtimeToken;
        this.reference = reference;
    }

    /**
     * 调用 Lua 函数，并把 Java 参数和返回值交给 native 桥转换。
     */
    public Object invoke(Object[] arguments, boolean waitForResult) {
        Object[] safeArguments = arguments == null ? new Object[0] : arguments;
        return nativeInvoke(runtimeToken, reference, safeArguments, waitForResult);
    }

    private static native Object nativeInvoke(
            long runtimeToken,
            int reference,
            Object[] arguments,
            boolean waitForResult
    );
}
