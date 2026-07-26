// 生成小鱼 m.findColors 的 JavaScript 代码。
//
// context 可用字段：
//   points[]              取色点列表（第 1 个是生效基准点）
//     enabled             是否启用
//     base                是否为生效基准点
//     x, y                绝对坐标
//     dx, dy              相对基准点偏移（本格式主要用这个）
//     hex                 颜色，如 "FF00AA"（无 0x）
//     delta               点偏色；"000000" 时不输出
//   region                范围（有框选用框选，否则整图）
//     left, top, right, bottom
//   image                 当前图片
//     width, height, path （临时图 path 可能为空）
//   direction             方向 1..8
//   defaultDelta          全局默认偏色，如 "000000"
//
// 必须定义 generate(context)，返回字符串。
function generate(context) {
    const colors = (context.points || [])
        .filter(point => point.enabled)
        .map(point => {
            let value = `${point.dx}|${point.dy}|${point.hex}`;
            if (point.delta && point.delta !== "000000") value += `-${point.delta}`;
            return value;
        })
        .join(",");
    const range = context.region;
    return `const [x, y] = m.findColors(${range.left}, ${range.top}, ${range.right}, ${range.bottom}, `
        + `${context.direction}, 0x${context.defaultDelta}, ${JSON.stringify(colors)});\n`
        + "console.log(x, y);";
}
