/**
 * 文件用途：声明单个图片标签的纯数据模型，集中管理图片、预览、范围、撤销和保存状态。
 */
#pragma once

#include <QImage>
#include <QObject>
#include <QRect>
#include <QString>
#include <QVector>

#include "model/color_point_model.h"

namespace xiaoyv::tools {

class ImageDocument final : public QObject {
    Q_OBJECT

public:
    enum class SourceKind {
        File,
        Temporary,
    };

    explicit ImageDocument(
            QImage image,
            QString filePath = {},
            QString displayName = {},
            QObject* parent = nullptr);

    const QImage& image() const;
    /** 返回当前应显示的图片；存在有效预览时返回预览，否则返回原图。 */
    const QImage& displayedImage() const;
    QString filePath() const;
    QString displayName() const;
    QString tabTitle() const;
    bool isTemporary() const;
    bool isModified() const;
    bool needsExitAttention() const;
    quint64 imageRevision() const;
    QRect selection() const;
    ColorPointModel* colorPoints();
    const ColorPointModel* colorPoints() const;

    bool canUndo() const;
    bool canRedo() const;
    bool hasPreview() const;
    bool previewMatches(quint64 sourceRevision) const;

    bool save(const QString& path, QString* error = nullptr);
    void setSelection(const QRect& selection);
    bool setPreview(QImage preview, quint64 sourceRevision);
    void clearPreview();
    bool applyPreview();
    void rotateLeft();
    void rotateRight();
    void flipHorizontal();
    void flipVertical();
    void undo();
    void redo();

signals:
    void imageChanged();
    void metadataChanged();
    void historyChanged();
    void selectionChanged(const QRect& selection);

private:
    struct HistoryState {
        QImage image;
        QRect selection;
    };

    void replaceImage(QImage image, QRect selection);
    void appendHistory();
    void trimHistory();
    void restoreHistory(int index);
    void updateModifiedState();
    QRect mapSelection(const std::function<QPoint(const QPoint&)>& mapper) const;

    QImage image_;
    QImage preview_;
    QString filePath_;
    QString displayName_;
    SourceKind sourceKind_ = SourceKind::Temporary;
    bool modified_ = false;
    quint64 revision_ = 1;
    quint64 previewRevision_ = 0;
    QRect selection_;
    QVector<HistoryState> history_;
    int historyIndex_ = 0;
    int savedHistoryIndex_ = -1;
    ColorPointModel colorPoints_;
};

} // namespace xiaoyv::tools
