-- 文件用途：设备临时验收一组兼容 YOLOv5 NCNN 模型；模型文件不随仓库分发。
local info, infoError = m.yolo.runtimeInfo()
assert(info, infoError)

local ok, loadError = m.yolo.init(
    "yolo_validation/coco.names",
    "yolo_validation/yolov5s_6.2.param",
    "yolo_validation/yolov5s_6.2.bin"
)
assert(ok, loadError)

local items, detectError = m.yolo.detect(
    "yolo_validation/bus.jpg",
    {probThreshold = 0.15, nmsThreshold = 0.45, targetSize = 640, threads = 2}
)
assert(items, detectError)
assert(#items > 0, "YOLO 真实模型检测结果为空")

local labels = {}
local foundBus = false
local foundPerson = false
for index, item in ipairs(items) do
    foundBus = foundBus or item.label == "bus"
    foundPerson = foundPerson or item.label == "person"
    labels[index] = string.format(
        "%s(%.3f,%.1f,%.1f,%.1f,%.1f)",
        tostring(item.label),
        item.prob,
        item.x,
        item.y,
        item.w,
        item.h
    )
end
print("YOLO 真实推理通过：", #items, table.concat(labels, "; "))
local released, releaseError = m.yolo.release()
assert(released, releaseError or "YOLO 模型释放失败")
assert(foundBus, "YOLO 真实模型结果缺少 bus")
assert(foundPerson, "YOLO 真实模型结果缺少 person")
