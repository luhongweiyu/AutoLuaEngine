/**
 * 文件用途：提供 Lua/后续 JS、Go 共用的 Java 类加载、反射调用和类型转换后端。
 */
package com.xiaoyv.engine.interop;

import android.content.Context;

import com.xiaoyv.engine.AndroidHostBridge;

import java.lang.reflect.Array;
import java.lang.reflect.Constructor;
import java.lang.reflect.Field;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;
import java.lang.reflect.Proxy;
import java.util.ArrayList;
import java.util.Collection;
import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;

/**
 * Java 动态互操作后端。
 *
 * native 层负责 Lua userdata 和元方法，本类负责只有 JVM 才掌握的反射信息：
 * 方法重载、构造函数、字段、数组、集合和接口代理。所有候选方法只扫描一次，
 * 参数转换仍按每次实际值执行，避免整数范围不同却错误复用上次选择。
 */
public final class JavaInteropBridge {
    private static final ConcurrentHashMap<MethodCacheKey, List<Method>> METHOD_CACHE =
            new ConcurrentHashMap<>();

    private JavaInteropBridge() {
    }

    /**
     * 根据完整类名加载 Java 类，并支持 primitive 与 Java 数组写法。
     */
    public static Class<?> resolveClass(String className) throws ClassNotFoundException {
        if (className == null || className.trim().isEmpty()) {
            throw new ClassNotFoundException("Java 类名为空");
        }

        String normalized = className.trim();
        Class<?> primitive = primitiveClass(normalized);
        if (primitive != null) {
            return primitive;
        }
        if (normalized.endsWith("[]")) {
            Class<?> component = resolveClass(normalized.substring(0, normalized.length() - 2));
            return Array.newInstance(component, 0).getClass();
        }

        return Class.forName(normalized, true, applicationClassLoader());
    }

    /**
     * 读取对象字段、静态字段、方法或公开内部类。
     */
    public static JavaMemberResult getMember(Object target, String memberName) throws Exception {
        if (target == null) {
            throw new NullPointerException("不能读取 null Java 对象的成员");
        }
        if (memberName == null || memberName.isEmpty()) {
            return JavaMemberResult.notFound();
        }

        boolean staticTarget = target instanceof Class<?>;
        Class<?> targetType = staticTarget ? (Class<?>) target : target.getClass();
        Object receiver = staticTarget ? null : target;

        Field field = findReadableField(targetType, memberName, staticTarget);
        if (field != null) {
            makeAccessible(field);
            return JavaMemberResult.value(field.get(receiver));
        }

        if (!methodsFor(targetType, memberName, staticTarget).isEmpty()) {
            return JavaMemberResult.method();
        }

        if (staticTarget) {
            Class<?> nestedClass = findNestedClass(targetType, memberName);
            if (nestedClass != null) {
                return JavaMemberResult.value(nestedClass);
            }
        }

        if (targetType.isArray() && "length".equals(memberName)) {
            return JavaMemberResult.value(Long.valueOf(Array.getLength(target)));
        }

        if (!staticTarget && target instanceof Map<?, ?>) {
            Map<?, ?> map = (Map<?, ?>) target;
            if (map.containsKey(memberName)) {
                return JavaMemberResult.value(map.get(memberName));
            }
        }

        return JavaMemberResult.notFound();
    }

    /**
     * 调用已经绑定目标对象和名称的 Java 方法，并执行确定性的重载匹配。
     */
    public static JavaInvocationResult invoke(
            Object target,
            String methodName,
            Object[] arguments
    ) throws Exception {
        if (target == null) {
            throw new NullPointerException("不能调用 null Java 对象的方法");
        }

        boolean staticTarget = target instanceof Class<?>;
        Class<?> targetType = staticTarget ? (Class<?>) target : target.getClass();
        Object receiver = staticTarget ? null : target;
        Object[] safeArguments = arguments == null ? new Object[0] : arguments;

        MethodMatch match = selectMethod(
                methodsFor(targetType, methodName, staticTarget),
                safeArguments
        );
        if (match == null) {
            throw new NoSuchMethodException(buildMethodError(targetType, methodName, safeArguments));
        }

        try {
            makeAccessible(match.method);
            Object value = match.method.invoke(receiver, match.arguments);
            return match.method.getReturnType() == Void.TYPE
                    ? JavaInvocationResult.noValue()
                    : JavaInvocationResult.value(value);
        } catch (InvocationTargetException exception) {
            throw unwrapInvocationException(exception);
        }
    }

    /**
     * 调用 Java 构造函数；接口、数组和 primitive 也沿用懒人精灵的可调用类语义。
     */
    public static JavaInvocationResult construct(
            Class<?> targetType,
            Object[] arguments
    ) throws Exception {
        if (targetType == null) {
            throw new NullPointerException("Java 构造类型为空");
        }

        Object[] safeArguments = arguments == null ? new Object[0] : arguments;
        if (targetType.isPrimitive()) {
            if (safeArguments.length != 1) {
                throw new IllegalArgumentException("primitive 类型构造只接受一个参数");
            }
            Conversion conversion = convertValue(safeArguments[0], targetType);
            if (!conversion.valid) {
                throw new IllegalArgumentException("参数不能转换为 " + targetType.getName());
            }
            return JavaInvocationResult.value(conversion.value);
        }

        if (targetType.isInterface()) {
            if (safeArguments.length != 1 || !isLuaCallbackSource(safeArguments[0])) {
                throw new IllegalArgumentException("Java 接口需要一个 Lua function 或 table 参数");
            }
            return JavaInvocationResult.value(createInterfaceProxy(targetType, safeArguments[0]));
        }

        if (targetType.isArray()) {
            if (safeArguments.length != 1) {
                throw new IllegalArgumentException("Java 数组构造只接受长度或 Lua table");
            }
            if (safeArguments[0] instanceof Number) {
                int length = checkedArrayLength((Number) safeArguments[0]);
                return JavaInvocationResult.value(Array.newInstance(targetType.getComponentType(), length));
            }
            Conversion conversion = convertValue(safeArguments[0], targetType);
            if (!conversion.valid) {
                throw new IllegalArgumentException("Lua table 不能转换为 " + targetType.getName());
            }
            return JavaInvocationResult.value(conversion.value);
        }

        ConstructorMatch match = selectConstructor(targetType.getConstructors(), safeArguments);
        if (match == null) {
            throw new NoSuchMethodException(buildConstructorError(targetType, safeArguments));
        }

        try {
            makeAccessible(match.constructor);
            return JavaInvocationResult.value(match.constructor.newInstance(match.arguments));
        } catch (InvocationTargetException exception) {
            throw unwrapInvocationException(exception);
        }
    }

    /**
     * 写入实例或静态字段；Map 的字符串键也支持 Lua 的点号赋值。
     */
    public static void setMember(Object target, String memberName, Object value) throws Exception {
        if (target == null) {
            throw new NullPointerException("不能写入 null Java 对象的成员");
        }

        boolean staticTarget = target instanceof Class<?>;
        Class<?> targetType = staticTarget ? (Class<?>) target : target.getClass();
        Object receiver = staticTarget ? null : target;
        Field field = findField(targetType, memberName, staticTarget);
        if (field == null) {
            if (!staticTarget && target instanceof Map<?, ?>) {
                @SuppressWarnings("unchecked")
                Map<Object, Object> map = (Map<Object, Object>) target;
                map.put(memberName, value);
                return;
            }
            throw new NoSuchFieldException(targetType.getName() + "." + memberName);
        }

        Conversion conversion = convertValue(value, field.getType());
        if (!conversion.valid) {
            throw new IllegalArgumentException("值不能转换为字段类型 " + field.getType().getName());
        }
        makeAccessible(field);
        field.set(receiver, conversion.value);
    }

    /**
     * 按 Java 下标读取数组、List 或 Map。Java 容器下标保持从 0 开始。
     */
    public static JavaInvocationResult getIndex(Object target, Object key) {
        if (target == null) {
            throw new NullPointerException("不能读取 null Java 对象的下标");
        }
        if (target.getClass().isArray()) {
            return JavaInvocationResult.value(Array.get(target, checkedIndex(key)));
        }
        if (target instanceof List<?>) {
            return JavaInvocationResult.value(((List<?>) target).get(checkedIndex(key)));
        }
        if (target instanceof Map<?, ?>) {
            return JavaInvocationResult.value(((Map<?, ?>) target).get(key));
        }
        throw new IllegalArgumentException(target.getClass().getName() + " 不支持下标读取");
    }

    /**
     * 按 Java 下标写入数组、List 或 Map。
     */
    public static void setIndex(Object target, Object key, Object value) throws Exception {
        if (target == null) {
            throw new NullPointerException("不能写入 null Java 对象的下标");
        }
        if (target.getClass().isArray()) {
            Class<?> componentType = target.getClass().getComponentType();
            Conversion conversion = convertValue(value, componentType);
            if (!conversion.valid) {
                throw new IllegalArgumentException("值不能转换为数组元素类型 " + componentType.getName());
            }
            Array.set(target, checkedIndex(key), conversion.value);
            return;
        }
        if (target instanceof List<?>) {
            @SuppressWarnings("unchecked")
            List<Object> list = (List<Object>) target;
            list.set(checkedIndex(key), value);
            return;
        }
        if (target instanceof Map<?, ?>) {
            @SuppressWarnings("unchecked")
            Map<Object, Object> map = (Map<Object, Object>) target;
            map.put(key, value);
            return;
        }
        throw new IllegalArgumentException(target.getClass().getName() + " 不支持下标写入");
    }

    /**
     * 返回 Java 数组、Collection、Map 或 CharSequence 的长度。
     */
    public static int length(Object target) {
        if (target == null) {
            return 0;
        }
        if (target.getClass().isArray()) {
            return Array.getLength(target);
        }
        if (target instanceof Collection<?>) {
            return ((Collection<?>) target).size();
        }
        if (target instanceof Map<?, ?>) {
            return ((Map<?, ?>) target).size();
        }
        if (target instanceof CharSequence) {
            return ((CharSequence) target).length();
        }
        throw new IllegalArgumentException(target.getClass().getName() + " 不支持长度操作");
    }

    /**
     * 使用 Java equals 实现 Lua userdata 的相等比较。
     */
    public static boolean objectsEqual(Object left, Object right) {
        return Objects.equals(left, right);
    }

    /**
     * 使用 Java toString 实现 Lua userdata 的字符串化。
     */
    public static String objectToString(Object target) {
        return String.valueOf(target);
    }

    /**
     * 把 Lua 接口回调返回值转换为 Java 方法声明的返回类型。
     */
    static Object convertCallbackResult(Object value, Class<?> returnType) {
        if (returnType == Void.TYPE) {
            return null;
        }
        if (value == null && returnType.isPrimitive()) {
            return defaultValue(returnType);
        }

        Conversion conversion = convertValue(value, returnType);
        if (!conversion.valid) {
            throw new IllegalArgumentException(
                    "Lua 回调返回值不能转换为 " + returnType.getName()
            );
        }
        return conversion.value;
    }

    /**
     * 返回 primitive 类型的默认值，供缺少 Lua 接口方法时使用。
     */
    static Object defaultValue(Class<?> type) {
        if (!type.isPrimitive() || type == Void.TYPE) {
            return null;
        }
        if (type == Boolean.TYPE) {
            return false;
        }
        if (type == Character.TYPE) {
            return '\0';
        }
        if (type == Byte.TYPE) {
            return (byte) 0;
        }
        if (type == Short.TYPE) {
            return (short) 0;
        }
        if (type == Integer.TYPE) {
            return 0;
        }
        if (type == Long.TYPE) {
            return 0L;
        }
        if (type == Float.TYPE) {
            return 0.0f;
        }
        return 0.0d;
    }

    /**
     * 从缓存读取指定名称的候选方法，缓存内容不依赖本次参数值。
     */
    private static List<Method> methodsFor(
            Class<?> targetType,
            String methodName,
            boolean staticTarget
    ) {
        MethodCacheKey key = new MethodCacheKey(targetType, methodName, staticTarget);
        List<Method> cached = METHOD_CACHE.get(key);
        if (cached != null) {
            return cached;
        }

        LinkedHashMap<String, Method> unique = new LinkedHashMap<>();
        for (Method method : targetType.getMethods()) {
            addMethodCandidate(unique, method, methodName, staticTarget);
        }

        for (Class<?> current = targetType; current != null; current = current.getSuperclass()) {
            for (Method method : current.getDeclaredMethods()) {
                addMethodCandidate(unique, method, methodName, staticTarget);
            }
        }

        ArrayList<Method> methods = new ArrayList<>(unique.values());
        Collections.sort(methods, (left, right) ->
                methodSignature(left).compareTo(methodSignature(right))
        );
        List<Method> immutable = Collections.unmodifiableList(methods);
        List<Method> previous = METHOD_CACHE.putIfAbsent(key, immutable);
        return previous == null ? immutable : previous;
    }

    /**
     * 按名称和静态属性收集一个方法，并以签名去重。
     */
    private static void addMethodCandidate(
            Map<String, Method> methods,
            Method method,
            String methodName,
            boolean staticTarget
    ) {
        if (!method.getName().equals(methodName)) {
            return;
        }
        if (Modifier.isStatic(method.getModifiers()) != staticTarget) {
            return;
        }
        methods.put(methodSignature(method), method);
    }

    /**
     * 为候选方法生成稳定签名，保证不同设备上的重载选择顺序一致。
     */
    private static String methodSignature(Method method) {
        StringBuilder builder = new StringBuilder(method.getName()).append('(');
        for (Class<?> parameterType : method.getParameterTypes()) {
            builder.append(parameterType.getName()).append(';');
        }
        return builder.append(')').append(method.getReturnType().getName()).toString();
    }

    /**
     * 从候选方法中选择总转换代价最低的一项。
     */
    private static MethodMatch selectMethod(List<Method> methods, Object[] arguments) {
        MethodMatch best = null;
        for (Method method : methods) {
            ConvertedArguments converted = convertArguments(
                    arguments,
                    method.getParameterTypes(),
                    method.isVarArgs()
            );
            if (!converted.valid) {
                continue;
            }

            MethodMatch candidate = new MethodMatch(method, converted.values, converted.score);
            if (best == null || candidate.score < best.score) {
                best = candidate;
            }
        }
        return best;
    }

    /**
     * 从公开构造函数中选择总转换代价最低的一项。
     */
    private static ConstructorMatch selectConstructor(
            Constructor<?>[] constructors,
            Object[] arguments
    ) {
        ArrayList<Constructor<?>> sorted = new ArrayList<>();
        Collections.addAll(sorted, constructors);
        Collections.sort(sorted, (left, right) ->
                constructorSignature(left).compareTo(constructorSignature(right))
        );

        ConstructorMatch best = null;
        for (Constructor<?> constructor : sorted) {
            ConvertedArguments converted = convertArguments(
                    arguments,
                    constructor.getParameterTypes(),
                    constructor.isVarArgs()
            );
            if (!converted.valid) {
                continue;
            }

            ConstructorMatch candidate = new ConstructorMatch(
                    constructor,
                    converted.values,
                    converted.score
            );
            if (best == null || candidate.score < best.score) {
                best = candidate;
            }
        }
        return best;
    }

    /**
     * 为构造函数生成稳定签名。
     */
    private static String constructorSignature(Constructor<?> constructor) {
        StringBuilder builder = new StringBuilder(constructor.getDeclaringClass().getName())
                .append('(');
        for (Class<?> parameterType : constructor.getParameterTypes()) {
            builder.append(parameterType.getName()).append(';');
        }
        return builder.append(')').toString();
    }

    /**
     * 按固定参数或 Java varargs 规则转换整组调用参数。
     */
    private static ConvertedArguments convertArguments(
            Object[] source,
            Class<?>[] targetTypes,
            boolean varArgs
    ) {
        if (!varArgs && source.length != targetTypes.length) {
            return ConvertedArguments.invalid();
        }
        if (varArgs && source.length < targetTypes.length - 1) {
            return ConvertedArguments.invalid();
        }

        Object[] result = new Object[targetTypes.length];
        int score = varArgs ? 20 : 0;
        int fixedCount = varArgs ? targetTypes.length - 1 : targetTypes.length;
        for (int index = 0; index < fixedCount; index++) {
            Conversion conversion = convertValue(source[index], targetTypes[index]);
            if (!conversion.valid) {
                return ConvertedArguments.invalid();
            }
            result[index] = conversion.value;
            score += conversion.score;
        }

        if (!varArgs) {
            return ConvertedArguments.valid(result, score);
        }

        // `Arrays.asList({"a", "b"})` 这类写法会把一个 Lua table 直接传给
        // Java varargs 数组。先尝试整数组转换，失败后再按普通可变参数展开。
        if (source.length == targetTypes.length) {
            Conversion directArray = convertValue(
                    source[source.length - 1],
                    targetTypes[targetTypes.length - 1]
            );
            if (directArray.valid) {
                Object[] directResult = result.clone();
                directResult[directResult.length - 1] = directArray.value;
                return ConvertedArguments.valid(
                        directResult,
                        score - 15 + directArray.score
                );
            }
        }

        Class<?> componentType = targetTypes[targetTypes.length - 1].getComponentType();
        int varargCount = source.length - fixedCount;
        Object varargArray = Array.newInstance(componentType, varargCount);
        for (int index = 0; index < varargCount; index++) {
            Conversion conversion = convertValue(source[fixedCount + index], componentType);
            if (!conversion.valid) {
                return ConvertedArguments.invalid();
            }
            Array.set(varargArray, index, conversion.value);
            score += conversion.score;
        }
        result[result.length - 1] = varargArray;
        return ConvertedArguments.valid(result, score);
    }

    /**
     * 将一个 Lua/JNI 中间值转换为指定 Java 类型，并返回用于重载排序的代价。
     */
    private static Conversion convertValue(Object value, Class<?> targetType) {
        if (value == null) {
            return targetType.isPrimitive()
                    ? Conversion.invalid()
                    : Conversion.valid(null, 8);
        }

        Class<?> boxedTarget = boxedType(targetType);
        if (boxedTarget.isInstance(value)) {
            if (boxedTarget == value.getClass()) {
                return Conversion.valid(value, targetType.isPrimitive() ? 1 : 0);
            }
            if (targetType == Object.class) {
                return Conversion.valid(value, 20);
            }
            return Conversion.valid(
                    value,
                    1 + inheritanceDistance(value.getClass(), boxedTarget)
            );
        }

        if (value instanceof Number && Number.class.isAssignableFrom(boxedTarget)) {
            return convertNumber((Number) value, targetType);
        }

        if (boxedTarget == Boolean.class && value instanceof Boolean) {
            return Conversion.valid(value, targetType.isPrimitive() ? 1 : 0);
        }

        if (boxedTarget == Character.class) {
            if (value instanceof Character) {
                return Conversion.valid(value, 0);
            }
            if (value instanceof String && ((String) value).length() == 1) {
                return Conversion.valid(((String) value).charAt(0), 2);
            }
        }

        if (value instanceof String) {
            String text = (String) value;
            if (targetType == String.class) {
                return Conversion.valid(text, 0);
            }
            if (targetType.isAssignableFrom(String.class)) {
                return Conversion.valid(text, 1);
            }
            if (targetType.isEnum()) {
                try {
                    @SuppressWarnings({"rawtypes", "unchecked"})
                    Object enumValue = Enum.valueOf((Class<? extends Enum>) targetType, text);
                    return Conversion.valid(enumValue, 3);
                } catch (IllegalArgumentException ignored) {
                    return Conversion.invalid();
                }
            }
        }

        if (value instanceof LuaCallback && targetType.isInterface()) {
            if (!isSingleAbstractMethodInterface(targetType)) {
                return Conversion.invalid();
            }
            return Conversion.valid(createInterfaceProxy(targetType, value), 3);
        }

        if (value instanceof LuaTableValue) {
            LuaTableValue table = (LuaTableValue) value;
            if (targetType.isArray()) {
                return convertTableToArray(table, targetType.getComponentType());
            }
            if (Map.class.isAssignableFrom(targetType)) {
                return convertTableToMap(table, targetType);
            }
            if (Collection.class.isAssignableFrom(targetType)) {
                return convertTableToCollection(table, targetType);
            }
            if (targetType.isInterface()) {
                return Conversion.valid(createInterfaceProxy(targetType, table), 3);
            }
        }

        if (targetType.isAssignableFrom(value.getClass())) {
            return Conversion.valid(value, inheritanceDistance(value.getClass(), targetType));
        }

        return Conversion.invalid();
    }

    /**
     * 按 Lua 数值特征转换 primitive/wrapper，并优先匹配常用 int 或 double。
     */
    private static Conversion convertNumber(Number number, Class<?> targetType) {
        Class<?> boxedTarget = boxedType(targetType);
        boolean integerSource = number instanceof Byte
                || number instanceof Short
                || number instanceof Integer
                || number instanceof Long;

        if (boxedTarget == Byte.class) {
            long value = number.longValue();
            return value >= Byte.MIN_VALUE && value <= Byte.MAX_VALUE
                    ? Conversion.valid(Byte.valueOf((byte) value), integerSource ? 4 : 8)
                    : Conversion.invalid();
        }
        if (boxedTarget == Short.class) {
            long value = number.longValue();
            return value >= Short.MIN_VALUE && value <= Short.MAX_VALUE
                    ? Conversion.valid(Short.valueOf((short) value), integerSource ? 3 : 8)
                    : Conversion.invalid();
        }
        if (boxedTarget == Integer.class) {
            long value = number.longValue();
            return value >= Integer.MIN_VALUE && value <= Integer.MAX_VALUE
                    ? Conversion.valid(Integer.valueOf((int) value), integerSource ? 0 : 7)
                    : Conversion.invalid();
        }
        if (boxedTarget == Long.class) {
            return Conversion.valid(Long.valueOf(number.longValue()), integerSource ? 1 : 6);
        }
        if (boxedTarget == Float.class) {
            return Conversion.valid(Float.valueOf(number.floatValue()), integerSource ? 5 : 1);
        }
        if (boxedTarget == Double.class) {
            return Conversion.valid(Double.valueOf(number.doubleValue()), integerSource ? 4 : 0);
        }
        return Conversion.invalid();
    }

    /**
     * 将 Lua table 的连续 1..n 数组部分转换为指定 Java 数组。
     */
    private static Conversion convertTableToArray(LuaTableValue table, Class<?> componentType) {
        int length = table.arrayLength();
        Object array = Array.newInstance(componentType, length);
        int score = 4;
        for (int index = 0; index < length; index++) {
            Object source = table.get(Long.valueOf(index + 1L));
            Conversion conversion = convertValue(source, componentType);
            if (!conversion.valid) {
                return Conversion.invalid();
            }
            Array.set(array, index, conversion.value);
            score += conversion.score;
        }
        return Conversion.valid(array, score);
    }

    /**
     * 将 Lua table 转换为目标 Map；接口或抽象类使用 LinkedHashMap。
     */
    private static Conversion convertTableToMap(LuaTableValue table, Class<?> targetType) {
        Map<Object, Object> result;
        if (targetType.isInterface() || Modifier.isAbstract(targetType.getModifiers())) {
            result = new LinkedHashMap<>();
        } else {
            try {
                @SuppressWarnings("unchecked")
                Map<Object, Object> created =
                        (Map<Object, Object>) targetType.getDeclaredConstructor().newInstance();
                result = created;
            } catch (Exception exception) {
                return Conversion.invalid();
            }
        }
        result.putAll(table.values());
        return Conversion.valid(result, 5);
    }

    /**
     * 将 Lua table 的连续数组部分转换为目标 Collection。
     */
    private static Conversion convertTableToCollection(
            LuaTableValue table,
            Class<?> targetType
    ) {
        Collection<Object> result;
        if (targetType.isInterface() || Modifier.isAbstract(targetType.getModifiers())) {
            result = Set.class.isAssignableFrom(targetType)
                    ? new LinkedHashSet<>()
                    : new ArrayList<>();
        } else {
            try {
                @SuppressWarnings("unchecked")
                Collection<Object> created =
                        (Collection<Object>) targetType.getDeclaredConstructor().newInstance();
                result = created;
            } catch (Exception exception) {
                return Conversion.invalid();
            }
        }

        int length = table.arrayLength();
        for (int index = 1; index <= length; index++) {
            result.add(table.get(Long.valueOf(index)));
        }
        return Conversion.valid(result, 5);
    }

    /**
     * 创建 Java 接口动态代理；bootstrap 类使用应用 ClassLoader 承载代理类。
     */
    private static Object createInterfaceProxy(Class<?> interfaceType, Object callbackSource) {
        ClassLoader classLoader = interfaceType.getClassLoader();
        if (classLoader == null) {
            classLoader = applicationClassLoader();
        }
        return Proxy.newProxyInstance(
                classLoader,
                new Class<?>[]{interfaceType},
                new LuaInterfaceInvocationHandler(interfaceType, callbackSource)
        );
    }

    /**
     * 判断接口是否只有一个需要实现的抽象方法。
     */
    private static boolean isSingleAbstractMethodInterface(Class<?> interfaceType) {
        int abstractMethodCount = 0;
        for (Method method : interfaceType.getMethods()) {
            if (method.getDeclaringClass() == Object.class
                    || Modifier.isStatic(method.getModifiers())) {
                continue;
            }
            if (Modifier.isAbstract(method.getModifiers())) {
                abstractMethodCount++;
            }
        }
        return abstractMethodCount == 1;
    }

    /**
     * 判断一个 JNI 中间值能否作为 Java 接口代理来源。
     */
    private static boolean isLuaCallbackSource(Object value) {
        return value instanceof LuaCallback || value instanceof LuaTableValue;
    }

    /**
     * 查找字段，并保证类对象只暴露静态字段。
     */
    private static Field findField(
            Class<?> targetType,
            String fieldName,
            boolean staticTarget
    ) {
        try {
            Field field = targetType.getField(fieldName);
            if (Modifier.isStatic(field.getModifiers()) == staticTarget) {
                return field;
            }
        } catch (NoSuchFieldException ignored) {
            // 继续查找当前类及父类声明字段。
        }

        for (Class<?> current = targetType; current != null; current = current.getSuperclass()) {
            try {
                Field field = current.getDeclaredField(fieldName);
                if (Modifier.isStatic(field.getModifiers()) == staticTarget) {
                    return field;
                }
            } catch (NoSuchFieldException ignored) {
                // 当前层级没有该字段，继续父类。
            }
        }
        return null;
    }

    /**
     * 查找 Lua 可读取的公开字段。
     *
     * 读取侧不能暴露 ArrayList.size 这类私有实现字段，否则会遮蔽同名公开方法；
     * 该顺序与懒人精灵旧实现使用 Class.getField 的行为一致。
     */
    private static Field findReadableField(
            Class<?> targetType,
            String fieldName,
            boolean staticTarget
    ) {
        try {
            Field field = targetType.getField(fieldName);
            return Modifier.isStatic(field.getModifiers()) == staticTarget ? field : null;
        } catch (NoSuchFieldException ignored) {
            return null;
        }
    }

    /**
     * 按简单名称查找公开或声明的内部类。
     */
    private static Class<?> findNestedClass(Class<?> targetType, String simpleName) {
        for (Class<?> nested : targetType.getClasses()) {
            if (nested.getSimpleName().equals(simpleName)) {
                return nested;
            }
        }
        for (Class<?> nested : targetType.getDeclaredClasses()) {
            if (nested.getSimpleName().equals(simpleName)) {
                return nested;
            }
        }
        return null;
    }

    /**
     * 在允许的设备上打开反射访问；公开成员即使失败仍可正常调用。
     */
    private static void makeAccessible(java.lang.reflect.AccessibleObject object) {
        try {
            object.setAccessible(true);
        } catch (RuntimeException ignored) {
            // Android 隐藏 API 可能拒绝 setAccessible，随后由真实调用给出明确异常。
        }
    }

    /**
     * 将 InvocationTargetException 还原为实际 Java 异常。
     */
    private static Exception unwrapInvocationException(InvocationTargetException exception) {
        Throwable cause = exception.getCause();
        if (cause instanceof Exception) {
            return (Exception) cause;
        }
        if (cause instanceof Error) {
            throw (Error) cause;
        }
        return exception;
    }

    /**
     * 返回应用 ClassLoader，供系统类和插件类统一创建代理。
     */
    private static ClassLoader applicationClassLoader() {
        Context context = AndroidHostBridge.applicationContext();
        if (context != null) {
            return context.getClassLoader();
        }
        ClassLoader contextLoader = Thread.currentThread().getContextClassLoader();
        return contextLoader == null ? JavaInteropBridge.class.getClassLoader() : contextLoader;
    }

    /**
     * 解析 Java primitive 名称。
     */
    private static Class<?> primitiveClass(String name) {
        switch (name) {
            case "boolean":
                return Boolean.TYPE;
            case "byte":
                return Byte.TYPE;
            case "char":
                return Character.TYPE;
            case "short":
                return Short.TYPE;
            case "int":
                return Integer.TYPE;
            case "long":
                return Long.TYPE;
            case "float":
                return Float.TYPE;
            case "double":
                return Double.TYPE;
            case "void":
                return Void.TYPE;
            default:
                return null;
        }
    }

    /**
     * 将 primitive 类型映射到对应包装类。
     */
    private static Class<?> boxedType(Class<?> type) {
        if (!type.isPrimitive()) {
            return type;
        }
        if (type == Boolean.TYPE) {
            return Boolean.class;
        }
        if (type == Byte.TYPE) {
            return Byte.class;
        }
        if (type == Character.TYPE) {
            return Character.class;
        }
        if (type == Short.TYPE) {
            return Short.class;
        }
        if (type == Integer.TYPE) {
            return Integer.class;
        }
        if (type == Long.TYPE) {
            return Long.class;
        }
        if (type == Float.TYPE) {
            return Float.class;
        }
        if (type == Double.TYPE) {
            return Double.class;
        }
        return Void.class;
    }

    /**
     * 计算可赋值类型间的大致继承距离，用于重载选择。
     */
    private static int inheritanceDistance(Class<?> source, Class<?> target) {
        if (source == target) {
            return 0;
        }
        if (target.isInterface()) {
            return 2;
        }
        int distance = 1;
        for (Class<?> current = source.getSuperclass(); current != null; current = current.getSuperclass()) {
            if (current == target) {
                return distance;
            }
            distance++;
        }
        return 10;
    }

    /**
     * 校验 Java 容器下标必须是整数。
     */
    private static int checkedIndex(Object value) {
        if (!(value instanceof Number)) {
            throw new IllegalArgumentException("Java 下标必须是整数");
        }
        long index = ((Number) value).longValue();
        if (index < Integer.MIN_VALUE || index > Integer.MAX_VALUE) {
            throw new IndexOutOfBoundsException("Java 下标超出 int 范围：" + index);
        }
        return (int) index;
    }

    /**
     * 校验数组构造长度。
     */
    private static int checkedArrayLength(Number value) {
        long length = value.longValue();
        if (length < 0 || length > Integer.MAX_VALUE) {
            throw new NegativeArraySizeException("无效数组长度：" + length);
        }
        return (int) length;
    }

    /**
     * 生成方法未匹配错误，列出实际 Lua 参数类型。
     */
    private static String buildMethodError(
            Class<?> targetType,
            String methodName,
            Object[] arguments
    ) {
        return "找不到匹配方法 " + targetType.getName() + "." + methodName
                + argumentTypes(arguments);
    }

    /**
     * 生成构造函数未匹配错误。
     */
    private static String buildConstructorError(Class<?> targetType, Object[] arguments) {
        return "找不到匹配构造函数 " + targetType.getName() + argumentTypes(arguments);
    }

    /**
     * 把实际参数类型格式化为可读文本。
     */
    private static String argumentTypes(Object[] arguments) {
        StringBuilder builder = new StringBuilder("(");
        for (int index = 0; index < arguments.length; index++) {
            if (index > 0) {
                builder.append(", ");
            }
            Object argument = arguments[index];
            builder.append(argument == null ? "null" : argument.getClass().getName());
        }
        return builder.append(')').toString();
    }

    /**
     * 方法缓存键；Class 使用对象身份即可区分不同插件 ClassLoader 的同名类。
     */
    private static final class MethodCacheKey {
        private final Class<?> targetType;
        private final String methodName;
        private final boolean staticTarget;

        private MethodCacheKey(Class<?> targetType, String methodName, boolean staticTarget) {
            this.targetType = targetType;
            this.methodName = methodName;
            this.staticTarget = staticTarget;
        }

        @Override
        public boolean equals(Object other) {
            if (this == other) {
                return true;
            }
            if (!(other instanceof MethodCacheKey)) {
                return false;
            }
            MethodCacheKey key = (MethodCacheKey) other;
            return staticTarget == key.staticTarget
                    && targetType == key.targetType
                    && methodName.equals(key.methodName);
        }

        @Override
        public int hashCode() {
            int result = System.identityHashCode(targetType);
            result = 31 * result + methodName.hashCode();
            result = 31 * result + (staticTarget ? 1 : 0);
            return result;
        }
    }

    /**
     * 单参数转换结果。
     */
    private static final class Conversion {
        private final boolean valid;
        private final Object value;
        private final int score;

        private Conversion(boolean valid, Object value, int score) {
            this.valid = valid;
            this.value = value;
            this.score = score;
        }

        private static Conversion valid(Object value, int score) {
            return new Conversion(true, value, score);
        }

        private static Conversion invalid() {
            return new Conversion(false, null, Integer.MAX_VALUE);
        }
    }

    /**
     * 整组参数转换结果。
     */
    private static final class ConvertedArguments {
        private final boolean valid;
        private final Object[] values;
        private final int score;

        private ConvertedArguments(boolean valid, Object[] values, int score) {
            this.valid = valid;
            this.values = values;
            this.score = score;
        }

        private static ConvertedArguments valid(Object[] values, int score) {
            return new ConvertedArguments(true, values, score);
        }

        private static ConvertedArguments invalid() {
            return new ConvertedArguments(false, null, Integer.MAX_VALUE);
        }
    }

    /**
     * 已选择的方法及其转换后参数。
     */
    private static final class MethodMatch {
        private final Method method;
        private final Object[] arguments;
        private final int score;

        private MethodMatch(Method method, Object[] arguments, int score) {
            this.method = method;
            this.arguments = arguments;
            this.score = score;
        }
    }

    /**
     * 已选择的构造函数及其转换后参数。
     */
    private static final class ConstructorMatch {
        private final Constructor<?> constructor;
        private final Object[] arguments;
        private final int score;

        private ConstructorMatch(Constructor<?> constructor, Object[] arguments, int score) {
            this.constructor = constructor;
            this.arguments = arguments;
            this.score = score;
        }
    }
}
