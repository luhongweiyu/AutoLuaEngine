---
params: "见方法表"
returns: "boolean 或 string?"
---

**方法名称：** `PaddleOcr` PP-OCRv4 Java 兼容接口。

**语法：** `PaddleOcr.loadModel(useOnnx)` 等，完整入口见下表。

**参数说明：**

各方法参数随入口而定。模型文件参数均为字符串路径，`bitmap` 为
`android.graphics.Bitmap`，`padding` 为非负整数，`r/g/b` 为颜色分量。

先导入兼容类：

```lua
import("com.nx.assist.lua.LuaEngine")
import("com.nx.assist.lua.PaddleOcr")
```

| 方法 | 参数 | 返回值 |
|---|---|---|
| `PaddleOcr.loadModel(useOnnx)` | `boolean` | ONNX 内置模型加载成功为 `true` |
| `PaddleOcr.loadOnnxModel(det, cls, rec, keys)` | 四个模型/字典路径 | 成功为 `true` |
| `PaddleOcr.loadNnccModel(detParam, recParam, detBin, recBin, keys)` | 五个 NCNN 文件路径 | 当前固定为 `false` |
| `PaddleOcr.detect(bitmap)` | `Bitmap` | JSON 数组字符串；失败为 `nil` |
| `PaddleOcr.detectWithPadding(bitmap, padding, r, g, b)` | Bitmap、边框宽度和 RGB | JSON 数组字符串；失败为 `nil` |

| 返回值 | 说明 |
|---|---|
| `boolean` | 模型加载是否成功。 |
| `string` | 识别成功时的 JSON 数组字符串。 |
| `nil` | Bitmap、模型或识别过程无效。 |

每个识别项包含：

| 字段 | 类型 | 说明 |
|---|---|---|
| `label` | `string` | 识别文字 |
| `confidence` | `number` | 识别置信度 |
| `x, y, w, h` | `integer` | 相对传入 Bitmap 的文字框 |

**详细说明：**

内置和自定义 ONNX 模型都进入小鱼精灵现有 RapidOCR/ONNX Runtime 模型缓存。
`detectWithPadding` 只回收内部创建的带边框副本，调用方仍应通过
`LuaEngine.releaseBmp(bitmap)` 释放原 Bitmap。当前 APK 没有 NCNN 运行时，因此
`loadModel(false)` 和 `loadNnccModel(...)` 会真实返回 `false`，不会把 ONNX 模型冒充成
NCNN。

**使用示例：**

```lua
assert(PaddleOcr.loadModel(true))
local bitmap = assert(LuaEngine.snapShot(0, 0, 0, 0))
local json = PaddleOcr.detect(bitmap)
LuaEngine.releaseBmp(bitmap)
print(json)
```
