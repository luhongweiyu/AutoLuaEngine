// 文件用途：生成可直接继续编辑的 JavaScript 坐标颜色数组。
function generate(context) {
    const lines = ["const points = ["];
    for (const point of context.points || []) {
        if (!point.enabled) continue;
        lines.push(`    { x: ${point.x}, y: ${point.y}, color: 0x${point.hex}, delta: 0x${point.delta} },`);
    }
    lines.push("];");
    return lines.join("\n");
}
