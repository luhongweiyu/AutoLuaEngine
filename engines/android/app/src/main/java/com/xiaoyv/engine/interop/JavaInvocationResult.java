/**
 * 文件用途：描述 Java 方法调用结果，并区分 void 与返回 null 两种不同语义。
 */
package com.xiaoyv.engine.interop;

/**
 * Java 方法或构造函数调用结果。
 *
 * void 方法在 Lua 中不返回值；返回类型为对象但实际返回 null 时，Lua 中应返回
 * 一个 nil。hasValue 用来保留这一区别。
 */
public final class JavaInvocationResult {
    public final boolean hasValue;
    public final Object value;

    private JavaInvocationResult(boolean hasValue, Object value) {
        this.hasValue = hasValue;
        this.value = value;
    }

    /**
     * 创建有返回值的结果；value 可以为 null。
     */
    public static JavaInvocationResult value(Object value) {
        return new JavaInvocationResult(true, value);
    }

    /**
     * 创建 Java void 方法对应的无返回值结果。
     */
    public static JavaInvocationResult noValue() {
        return new JavaInvocationResult(false, null);
    }
}
