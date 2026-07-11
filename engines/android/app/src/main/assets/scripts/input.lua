-- 文件用途：内置示例脚本，用于验证 Root 输入注入 API。
print("run env =", getRunEnvType())

local ok = keyPress("Back")
print("keyPress Back =", ok)

-- 文本输入需要当前界面已有可输入焦点。
-- print("inputText =", inputText("hello nice!!!"))

-- 触摸示例会点击屏幕坐标，默认不自动执行，避免误触。
-- touchDown(0, 100, 100)
-- sleep(50)
-- print("touchMove =", touchMove(0, 200, 200))
-- touchUp(0)
