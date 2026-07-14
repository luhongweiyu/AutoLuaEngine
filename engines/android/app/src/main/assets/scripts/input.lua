-- 文件用途：内置示例脚本，用于验证 Root 输入注入 API。
print("运行环境 =", getRunEnvType())

local ok = keyPress("Back")
print("按下返回键 =", ok)

-- 文本输入需要当前界面已有可输入焦点。
-- print("输入文字 =", inputText("你好，小鱼精灵！"))

-- 触摸示例会点击屏幕坐标，默认不自动执行，避免误触。
-- touchDown(0, 100, 100)
-- sleep(50)
-- print("移动手指 =", touchMove(0, 200, 200))
-- touchUp(0)
