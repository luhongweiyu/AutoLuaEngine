---
params: "className: string"
returns: "无"
---
<!-- 标准化脚本文档：保留并扩展原有正文，不删除既有说明。 -->

**方法名称：** 导入 Java 类/包。

**语法：** `import(className)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `className` | `string` | 是 | 具体取值和组合规则见下方详细说明。 |

| 返回值 | 说明 |
|---|---|
| 无 | 此方法不返回值。 |

**使用示例：**

```lua
import("java.lang.String")
```

**详细说明：**

导入 Java 类或包。

- 完整类名导入：`import("android.content.Context")`。
- 包通配导入：`import("java.lang.*")`，首次访问 `Math` 时延迟加载并缓存到 `_G`。
- 静态字段、静态方法和公开内部类。
- Java 构造函数、实例字段和实例方法。
- 点号调用：`context.getPackageName()`，不要求 Lua 冒号语法。
- Java 方法链式返回：`builder.append("a").append("b")`。
- 方法和构造函数重载匹配，候选反射结果按类和方法名缓存。
- Java 数组、`List`、`Map` 下标及 `#object` 长度。
- Lua function 转单抽象方法接口，例如 `Runnable`、`Comparator`。
- Lua table 按方法名转多方法 Java 接口监听器。
- Java 异常转换为 Lua 错误，可用 `pcall` 捕获。
- `nil` 与 Java `null` 双向转换；Java `void` 方法在 Lua 中没有返回值。

| Lua | Java 参数 |
|---|---|
| `nil` | 非 primitive 的 `null` |
| boolean | `boolean` / `Boolean` |
| integer | 按范围匹配 `int`、`long` 等整数类型 |
| number | 优先匹配 `double`，也可转换为 `float` |
| string | `String` / `CharSequence` / 单字符 `char` |
| Java userdata | 原 Java 对象 |
| table | Java 数组、`List`、`Collection`、`Map` 或接口代理 |
| function | 单抽象方法 Java 接口代理 |

Java `String`、boolean、整数和浮点数返回 Lua 基础类型。其他 Java 返回对象继续
保持 userdata，因此可以继续读取成员或作为下一个 Java 方法的参数。

Lua table 转 Java 数组或集合时读取 `1..n` 连续部分；Java 数组和 `List` 在 Lua
中保持 Java 下标规则，从 `0` 开始。

Java 对象和接口代理由当前脚本运行期管理。脚本结束后相关引用会统一释放；旧代理再次触发时
会明确报“脚本运行时已结束”。

Java 接口回调会回到当前脚本运行环境中依次执行。同步 Java 方法会等回调完成后继续返回；
普通异步监听器进入脚本回调队列。回调错误按 Lua 错误处理，并写入脚本日志。
