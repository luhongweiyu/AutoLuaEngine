// 生成 JavaScript 坐标颜色数组。
//
// context 可用字段：
//   points[]              取色点列表（第 1 个是生效基准点）
//     enabled             是否启用
//     base                是否为生效基准点
//     x, y                绝对坐标
//     dx, dy              相对基准点偏移
//     hex                 颜色，如 "FF00AA"（无 0x）
//     delta               点偏色，如 "000000"
//   region                范围（有框选用框选，否则整图）
//     left, top, right, bottom
//   image                 当前图片
//     width, height, path （临时图 path 可能为空）
//   direction             方向 1..8
//   defaultDelta          全局默认偏色，如 "000000"
//
// 必须定义 generate(context)，返回字符串。
function generate(context) {
    const lines = ["const points = ["];
    for (const point of context.points || []) {
        if (!point.enabled) continue;
        lines.push(`    { x: ${point.x}, y: ${point.y}, color: 0x${point.hex}, delta: 0x${point.delta} },`);
    }
    lines.push("];");
    return lines.join("\n");
}
