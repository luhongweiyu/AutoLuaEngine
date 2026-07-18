/**
 * 文件用途：统一解析和格式化图片闭区间，避免取色与字库面板各自维护一套坐标规则。
 */
#pragma once

#include <QRect>
#include <QString>
#include <QStringList>

namespace xiaoyv::tools {

/** 把空矩形显示为“全图”，其他矩形显示为 left,top,right,bottom。 */
inline QString formatSelectionRange(const QRect& range) {
    if (range.isNull()) return QString::fromUtf8("全图");
    return QStringLiteral("%1,%2,%3,%4")
            .arg(range.left())
            .arg(range.top())
            .arg(range.right())
            .arg(range.bottom());
}

/**
 * 解析用户输入的闭区间。
 *
 * 这里仅检查语法和坐标顺序，不依据当前图片尺寸修改输入；真正读取图片时再与图片边界求交集。
 */
inline bool parseSelectionRange(const QString& text, QRect* range, QString* error = nullptr) {
    if (range == nullptr) {
        if (error != nullptr) *error = QString::fromUtf8("范围输出对象为空");
        return false;
    }
    const QString value = text.trimmed();
    if (value.isEmpty() || value.compare(QString::fromUtf8("全图"), Qt::CaseInsensitive) == 0) {
        *range = {};
        if (error != nullptr) error->clear();
        return true;
    }

    const QStringList parts = value.split(QLatin1Char(','));
    if (parts.size() != 4) {
        if (error != nullptr) *error = QString::fromUtf8("范围格式应为 left,top,right,bottom");
        return false;
    }
    int values[4]{};
    for (int index = 0; index < 4; ++index) {
        bool ok = false;
        values[index] = parts[index].trimmed().toInt(&ok);
        if (!ok) {
            if (error != nullptr) *error = QString::fromUtf8("范围坐标必须是整数");
            return false;
        }
    }
    if (values[0] > values[2] || values[1] > values[3]) {
        if (error != nullptr) *error = QString::fromUtf8("范围左上角不能位于右下角之外");
        return false;
    }
    *range = QRect(QPoint(values[0], values[1]), QPoint(values[2], values[3]));
    if (error != nullptr) error->clear();
    return true;
}

/** 返回实际读取图片时使用的范围；空范围代表整张图片。 */
inline QRect effectiveSelectionRange(const QRect& requested, const QRect& imageRect) {
    return requested.isNull() ? imageRect : requested.intersected(imageRect);
}

} // namespace xiaoyv::tools
