-- 文件用途：内置示例脚本，用于验证点击、滑动和按键自动化能力。
print("touch script started")
print("root available =", m.device.isRootAvailable())

local ok, err = m.touch.tap(100, 100)
if ok then
    print("touch.tap success")
else
    print("touch.tap failed =", err)
end

local swipeOk, swipeErr = m.touch.swipe(100, 300, 500, 300, 300)
if swipeOk then
    print("touch.swipe success")
else
    print("touch.swipe failed =", swipeErr)
end

print("accessibility enabled =", m.key.isAccessibilityEnabled())
print("root-first touch/key registered")
