// 文件用途：把统一取色上下文生成小鱼 JavaScript 多点找色代码。
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
