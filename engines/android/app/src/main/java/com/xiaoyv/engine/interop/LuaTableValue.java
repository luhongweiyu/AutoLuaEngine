/**
 * 文件用途：在 JNI 边界保留 Lua table 的键值结构，供 Java 参数匹配时转换类型。
 */
package com.xiaoyv.engine.interop;

import java.util.LinkedHashMap;
import java.util.Map;

/**
 * Lua table 的 Java 中间表示。
 *
 * 不在 native 中提前猜测 table 是数组还是 Map。Java 反射已经知道目标参数类型，
 * 因此可以在匹配具体方法时再转换为数组、List、Map 或接口代理。
 */
public final class LuaTableValue {
    private final LinkedHashMap<Object, Object> values = new LinkedHashMap<>();

    /**
     * 写入一个已经完成 Lua 到 Java 基础转换的键值对。
     */
    public void put(Object key, Object value) {
        values.put(key, value);
    }

    /**
     * 读取指定键；主要供接口代理按 Java 方法名查找 Lua 回调。
     */
    public Object get(Object key) {
        return values.get(key);
    }

    /**
     * 返回 Lua 数组部分的连续长度，索引从 1 开始。
     */
    public int arrayLength() {
        int length = 0;
        while (values.containsKey(Long.valueOf(length + 1L))) {
            length++;
        }
        return length;
    }

    /**
     * 返回只读用途的原始键值集合；转换过程不会修改 LuaTableValue。
     */
    public Map<Object, Object> values() {
        return values;
    }
}
