/**
 * 文件用途：描述 Lua 读取 Java 对象成员时的查找结果，区分字段值、方法和未找到。
 */
package com.autolua.engine.interop;

/**
 * Java 成员读取结果。
 *
 * native Lua 桥会根据 kind 决定把 value 压入 Lua，还是创建一个已经绑定目标对象
 * 和方法名的 Lua 闭包。字段值允许为 null，因此不能只靠 value 是否为空判断结果。
 */
public final class JavaMemberResult {
    public static final int KIND_NOT_FOUND = 0;
    public static final int KIND_VALUE = 1;
    public static final int KIND_METHOD = 2;

    public final int kind;
    public final Object value;

    private JavaMemberResult(int kind, Object value) {
        this.kind = kind;
        this.value = value;
    }

    /**
     * 创建“未找到”结果。
     */
    public static JavaMemberResult notFound() {
        return new JavaMemberResult(KIND_NOT_FOUND, null);
    }

    /**
     * 创建字段、内部类或特殊属性结果。
     */
    public static JavaMemberResult value(Object value) {
        return new JavaMemberResult(KIND_VALUE, value);
    }

    /**
     * 创建方法结果；方法本身由 native 包装为绑定闭包。
     */
    public static JavaMemberResult method() {
        return new JavaMemberResult(KIND_METHOD, null);
    }
}
