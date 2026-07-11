/**
 * 文件用途：把 Lua function/table 包装成 Java 接口代理，支持监听器和 Runnable 回调。
 */
package com.autolua.engine.interop;

import java.lang.reflect.InvocationHandler;
import java.lang.reflect.Method;

/**
 * Java 动态接口代理处理器。
 *
 * Lua table 按 Java 方法名保存回调；Lua function 只用于单抽象方法接口。Java 的
 * equals/hashCode/toString 在这里本地处理，避免无意义地回调脚本。
 */
final class LuaInterfaceInvocationHandler implements InvocationHandler {
    private final Class<?> interfaceType;
    private final Object callbackSource;

    LuaInterfaceInvocationHandler(Class<?> interfaceType, Object callbackSource) {
        this.interfaceType = interfaceType;
        this.callbackSource = callbackSource;
    }

    /**
     * 将一次 Java 接口调用分发到对应 Lua 函数。
     */
    @Override
    public Object invoke(Object proxy, Method method, Object[] arguments) {
        if (method.getDeclaringClass() == Object.class) {
            return invokeObjectMethod(proxy, method, arguments);
        }

        LuaCallback callback = resolveCallback(method.getName());
        if (callback == null) {
            return JavaInteropBridge.defaultValue(method.getReturnType());
        }

        Object value = callback.invoke(arguments, method.getReturnType() != Void.TYPE);
        return JavaInteropBridge.convertCallbackResult(value, method.getReturnType());
    }

    /**
     * 从单函数或按方法名组织的 Lua table 中找到实际回调。
     */
    private LuaCallback resolveCallback(String methodName) {
        if (callbackSource instanceof LuaCallback) {
            return (LuaCallback) callbackSource;
        }
        if (!(callbackSource instanceof LuaTableValue)) {
            return null;
        }

        Object value = ((LuaTableValue) callbackSource).get(methodName);
        return value instanceof LuaCallback ? (LuaCallback) value : null;
    }

    /**
     * 在 Java 侧完成 Object 基础方法，保持代理对象身份稳定。
     */
    private Object invokeObjectMethod(Object proxy, Method method, Object[] arguments) {
        String name = method.getName();
        if ("toString".equals(name)) {
            return "LuaProxy(" + interfaceType.getName() + ")";
        }
        if ("hashCode".equals(name)) {
            return System.identityHashCode(proxy);
        }
        if ("equals".equals(name)) {
            return arguments != null && arguments.length == 1 && proxy == arguments[0];
        }
        return null;
    }
}
