-- 文件用途：在已推送 RapidOCR ONNX 模型到 /sdcard/xiaoyv/models 后，验证截图保存和 OCR 全链路。
local imagePath = "/sdcard/xiaoyv/scripts/ocr_probe.png"
local modelDir = "/sdcard/xiaoyv/models"

local saved, saveError = m.capture(imagePath)
print("保存截图", saved, saveError)

local loaded, loadError = m.ocr.load(
    "probe",
    modelDir .. "/dbnet.onnx",
    modelDir .. "/crnn_lite_lstm.onnx",
    modelDir .. "/angle_net.onnx",
    modelDir .. "/keys.txt",
    2
)
print("加载 OCR 模型", loaded, loadError)
if loaded then
    print("重复加载 OCR 模型", m.ocr.load(
        "probe",
        modelDir .. "/dbnet.onnx",
        modelDir .. "/crnn_lite_lstm.onnx",
        modelDir .. "/angle_net.onnx",
        modelDir .. "/keys.txt",
        2
    ))
end

if loaded then
    local result, readError = m.ocr.read("probe", imagePath, {useAngle = true})
    print("OCR 结果", result and result.items and #result.items or 0, readError)
    if result and result.items then
        for _, item in ipairs(result.items) do
            print(item.text, item.x, item.y, item.w, item.h, item.score)
        end
    end
    local found, findError = m.ocr.findText("probe", imagePath, "设置", {
        exact = true,
        useAngle = true,
    })
    print("OCR 找文字", found and found.found, found and found.x, found and found.y, findError)
    print("释放 OCR 模型", m.ocr.release("probe"))
end
