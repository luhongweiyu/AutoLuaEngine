-- 文件用途：回归验证 Lua native 多线程的共享 _G、等待切换、停止和公平调度。
local shared = {
    count = 0,
    finished = 0,
}

for threadId = 1, 3 do
    m.thread.beginThread(function(id)
        for _ = 1, 5 do
            shared.count = shared.count + id
            sleep(10)
        end
        shared.finished = shared.finished + 1
    end, threadId)
end

while shared.finished < 3 do
    sleep(5)
end
assert(shared.count == 30, "子线程共享 table 结果错误")

local runningCount = 0
local thread = m.thread.newThread(function()
    while true do
        runningCount = runningCount + 1
        sleep(5)
    end
end)

sleep(30)
thread:stopThread()
local stoppedCount = runningCount
sleep(30)
assert(runningCount == stoppedCount, "stopThread 后子线程仍在运行")

local cpuFinished = false
m.thread.beginThread(function()
    local value = 0
    for _ = 1, 300000 do
        value = value + 1
    end
    cpuFinished = value == 300000
end)

local mainLoopCount = 0
while not cpuFinished and mainLoopCount < 1000000 do
    mainLoopCount = mainLoopCount + 1
end
assert(cpuFinished, "纯 Lua 计算任务没有获得公平调度")

print("Lua 多线程回归测试通过")
