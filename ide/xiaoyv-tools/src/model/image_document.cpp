/**
 * 文件用途：实现图片文档状态机，确保预览、撤销、临时图片和文件图片的行为相互独立。
 */
#include "model/image_document.h"

#include <QFileInfo>
#include <QImageWriter>
#include <QTransform>

#include <algorithm>
#include <functional>

namespace xiaoyv::tools {
namespace {

constexpr int kMaximumHistoryStates = 24;
constexpr qint64 kMaximumHistoryBytes = 256LL * 1024 * 1024;

qint64 imageBytes(const QImage& image) {
    return static_cast<qint64>(image.sizeInBytes());
}

} // namespace

ImageDocument::ImageDocument(
        QImage image,
        QString filePath,
        QString displayName,
        QObject* parent)
        : QObject(parent),
          image_(std::move(image)),
          filePath_(std::move(filePath)),
          displayName_(std::move(displayName)),
          sourceKind_(filePath_.isEmpty() ? SourceKind::Temporary : SourceKind::File),
          colorPoints_(this) {
    if (displayName_.isEmpty()) {
        displayName_ = filePath_.isEmpty()
                ? QString::fromUtf8("未命名图片")
                : QFileInfo(filePath_).fileName();
    }
    history_.push_back({image_, selection_});
    savedHistoryIndex_ = sourceKind_ == SourceKind::File ? 0 : -1;
}

const QImage& ImageDocument::image() const {
    return image_;
}

const QImage& ImageDocument::displayedImage() const {
    return hasPreview() ? preview_ : image_;
}

QString ImageDocument::filePath() const {
    return filePath_;
}

QString ImageDocument::displayName() const {
    return displayName_;
}

QString ImageDocument::tabTitle() const {
    return (isTemporary() || isModified()) ? displayName_ + QLatin1Char('*') : displayName_;
}

bool ImageDocument::isTemporary() const {
    return sourceKind_ == SourceKind::Temporary;
}

bool ImageDocument::isModified() const {
    return modified_;
}

bool ImageDocument::needsExitAttention() const {
    return isTemporary() || isModified();
}

quint64 ImageDocument::imageRevision() const {
    return revision_;
}

QRect ImageDocument::selection() const {
    return selection_;
}

ColorPointModel* ImageDocument::colorPoints() {
    return &colorPoints_;
}

const ColorPointModel* ImageDocument::colorPoints() const {
    return &colorPoints_;
}

bool ImageDocument::canUndo() const {
    return historyIndex_ > 0;
}

bool ImageDocument::canRedo() const {
    return historyIndex_ + 1 < history_.size();
}

bool ImageDocument::hasPreview() const {
    return !preview_.isNull() && previewRevision_ == revision_;
}

bool ImageDocument::previewMatches(quint64 sourceRevision) const {
    return hasPreview() && previewRevision_ == sourceRevision;
}

bool ImageDocument::save(const QString& path, QString* error) {
    if (path.trimmed().isEmpty()) {
        if (error != nullptr) *error = QString::fromUtf8("保存路径为空");
        return false;
    }
    QImageWriter writer(path);
    writer.setQuality(95);
    if (!writer.write(image_)) {
        if (error != nullptr) *error = writer.errorString();
        return false;
    }
    filePath_ = path;
    displayName_ = QFileInfo(path).fileName();
    sourceKind_ = SourceKind::File;
    savedHistoryIndex_ = historyIndex_;
    updateModifiedState();
    emit metadataChanged();
    if (error != nullptr) error->clear();
    return true;
}

void ImageDocument::setSelection(const QRect& selection) {
    if (selection_ == selection) return;
    selection_ = selection;
    if (historyIndex_ >= 0 && historyIndex_ < history_.size()) {
        history_[historyIndex_].selection = selection_;
    }
    emit selectionChanged(selection_);
}

bool ImageDocument::setPreview(QImage preview, quint64 sourceRevision) {
    if (preview.isNull() || sourceRevision != revision_ || preview.size() != image_.size()) {
        return false;
    }
    preview_ = std::move(preview);
    previewRevision_ = sourceRevision;
    emit imageChanged();
    return true;
}

void ImageDocument::clearPreview() {
    if (preview_.isNull() && previewRevision_ == 0) return;
    preview_ = {};
    previewRevision_ = 0;
    emit imageChanged();
}

bool ImageDocument::applyPreview() {
    if (!hasPreview()) return false;
    QImage applied = preview_;
    preview_ = {};
    previewRevision_ = 0;
    replaceImage(std::move(applied), selection_);
    return true;
}

void ImageDocument::rotateLeft() {
    if (image_.isNull()) return;
    const int oldWidth = image_.width();
    const QRect mapped = mapSelection([oldWidth](const QPoint& point) {
        return QPoint(point.y(), oldWidth - 1 - point.x());
    });
    replaceImage(image_.transformed(QTransform().rotate(-90)), mapped);
}

void ImageDocument::rotateRight() {
    if (image_.isNull()) return;
    const int oldHeight = image_.height();
    const QRect mapped = mapSelection([oldHeight](const QPoint& point) {
        return QPoint(oldHeight - 1 - point.y(), point.x());
    });
    replaceImage(image_.transformed(QTransform().rotate(90)), mapped);
}

void ImageDocument::flipHorizontal() {
    if (image_.isNull()) return;
    const int oldWidth = image_.width();
    const QRect mapped = mapSelection([oldWidth](const QPoint& point) {
        return QPoint(oldWidth - 1 - point.x(), point.y());
    });
    replaceImage(image_.mirrored(true, false), mapped);
}

void ImageDocument::flipVertical() {
    if (image_.isNull()) return;
    const int oldHeight = image_.height();
    const QRect mapped = mapSelection([oldHeight](const QPoint& point) {
        return QPoint(point.x(), oldHeight - 1 - point.y());
    });
    replaceImage(image_.mirrored(false, true), mapped);
}

void ImageDocument::undo() {
    if (canUndo()) restoreHistory(historyIndex_ - 1);
}

void ImageDocument::redo() {
    if (canRedo()) restoreHistory(historyIndex_ + 1);
}

void ImageDocument::replaceImage(QImage image, QRect selection) {
    if (image.isNull()) return;
    preview_ = {};
    previewRevision_ = 0;
    image_ = std::move(image);
    selection_ = selection;
    ++revision_;
    appendHistory();
    updateModifiedState();
    emit imageChanged();
    emit selectionChanged(selection_);
    emit historyChanged();
    emit metadataChanged();
}

void ImageDocument::appendHistory() {
    while (history_.size() > historyIndex_ + 1) history_.removeLast();
    history_.push_back({image_, selection_});
    historyIndex_ = history_.size() - 1;
    trimHistory();
}

void ImageDocument::trimHistory() {
    qint64 totalBytes = 0;
    for (const HistoryState& state : history_) totalBytes += imageBytes(state.image);
    while (history_.size() > 1
            && (history_.size() > kMaximumHistoryStates || totalBytes > kMaximumHistoryBytes)) {
        totalBytes -= imageBytes(history_.front().image);
        history_.removeFirst();
        --historyIndex_;
        if (savedHistoryIndex_ >= 0) --savedHistoryIndex_;
        if (savedHistoryIndex_ < 0) savedHistoryIndex_ = -1;
    }
}

void ImageDocument::restoreHistory(int index) {
    if (index < 0 || index >= history_.size() || index == historyIndex_) return;
    preview_ = {};
    previewRevision_ = 0;
    historyIndex_ = index;
    image_ = history_[index].image;
    selection_ = history_[index].selection;
    ++revision_;
    updateModifiedState();
    emit imageChanged();
    emit selectionChanged(selection_);
    emit historyChanged();
    emit metadataChanged();
}

void ImageDocument::updateModifiedState() {
    const bool value = sourceKind_ == SourceKind::File && historyIndex_ != savedHistoryIndex_;
    if (modified_ == value) return;
    modified_ = value;
    emit metadataChanged();
}

QRect ImageDocument::mapSelection(const std::function<QPoint(const QPoint&)>& mapper) const {
    if (selection_.isNull()) return {};
    const QPoint points[] = {
            mapper(selection_.topLeft()),
            mapper(selection_.topRight()),
            mapper(selection_.bottomLeft()),
            mapper(selection_.bottomRight()),
    };
    int left = points[0].x();
    int right = points[0].x();
    int top = points[0].y();
    int bottom = points[0].y();
    for (const QPoint& point : points) {
        left = std::min(left, point.x());
        right = std::max(right, point.x());
        top = std::min(top, point.y());
        bottom = std::max(bottom, point.y());
    }
    return QRect(QPoint(left, top), QPoint(right, bottom));
}

} // namespace xiaoyv::tools
