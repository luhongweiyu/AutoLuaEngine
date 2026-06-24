print("touch script started")

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
print("key.back/key.home registered")
