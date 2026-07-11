-- Java import 兼容示例：类、Context、集合、接口回调和 Java 层 HTTP。
import("java.lang.*")
import("java.util.*")
import("android.content.Context")
import("com.nx.assist.lua.LuaEngine")

print("Math.sin =", Math.sin(1.2))
print("系统时间 =", System.currentTimeMillis())

local context = LuaEngine.getContext()
print("包名 =", context.getPackageName())
print("Activity 服务名 =", Context.ACTIVITY_SERVICE)

local values = ArrayList()
values.add("b")
values.add("a")

local comparator = Comparator(function(left, right)
    if left == right then
        return 0
    end
    return left < right and -1 or 1
end)

Collections.sort(values, comparator)
print("排序结果 =", values[0], values[1])

local response = LuaEngine.httpGet("http://127.0.0.1:18380/health", {}, 3)
print("引擎 HTTP 状态 =", response)
