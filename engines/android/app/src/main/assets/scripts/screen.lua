-- 文件用途：内置示例脚本，用于验证截图缓存帧和取色能力。
print("screen script started")

local img, err = m.screen.capture()
if img then
    print("screen.capture success")
    print("image id =", img.id)
    print("size =", img.width, img.height)
    print("format =", img.format)
    print("source =", img.source)
    print("captureDurationMs =", img.captureDurationMs)
    print("byteLength =", img.byteLength)

    local centerX = math.floor(img.width / 2)
    local centerY = math.floor(img.height / 2)
    local rgb, r, g, b, a = m.image.getPixel(img, centerX, centerY)
    if rgb then
        print("center pixel =", rgb, r, g, b, a)
    else
        print("image.getPixel failed =", r)
    end

    local points = {
        { x = 0, y = 0 },
        { centerX, centerY },
        { x = img.width - 1, y = img.height - 1 },
    }
    local colors, colorsErr = m.image.getPixels(img, points)
    if colors then
        print("image.getPixels count =", #colors)
    else
        print("image.getPixels failed =", colorsErr)
    end

    print("screen frame is managed by engine")
else
    print("screen.capture failed =", err)
end
