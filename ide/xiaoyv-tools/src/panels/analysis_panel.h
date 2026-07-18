/**
 * 文件用途：声明二值化面板及“参数变化即预览、只接受当前图片版本”异步状态机。
 */
#pragma once

#include <QFutureWatcher>
#include <QMetaObject>
#include <QPointer>
#include <QWidget>

#include <optional>

#include "core/image_processing.h"

class QCheckBox;
class QLineEdit;
class QRadioButton;
class QSlider;
class QSpinBox;

namespace xiaoyv::tools {

class ImageDocument;

struct AnalysisTaskResult {
    quint64 requestId = 0;
    QPointer<ImageDocument> document;
    quint64 sourceRevision = 0;
    BinarySettings settings;
    bool applyWhenReady = false;
    QImage preview;
    QString error;
};

class AnalysisPanel final : public QWidget {
    Q_OBJECT

public:
    explicit AnalysisPanel(QWidget* parent = nullptr);
    ~AnalysisPanel() override;

    void setDocument(ImageDocument* document);
    /** 仅二值化标签可见时启用实时预览；停用会立即丢弃任务并恢复原图显示。 */
    void setPreviewActive(bool active);

signals:
    void statusMessage(const QString& message);

private:
    AnalysisTaskResult makeRequest(bool applyWhenReady);
    BinarySettings currentSettings() const;
    void syncColorRulesFromDocument();
    void settingsChanged();
    void updateModeControls();
    void requestPreview(bool applyWhenReady);
    void startPendingRequest();
    void handleFinishedRequest();
    void invalidatePreview(bool clearVisiblePreview);
    void applyPreview();

    QPointer<ImageDocument> document_;
    QMetaObject::Connection colorPointsConnection_;
    QRadioButton* colorMode_ = nullptr;
    QRadioButton* grayMode_ = nullptr;
    QLineEdit* colorRulesEdit_ = nullptr;
    QSlider* graySlider_ = nullptr;
    QSpinBox* graySpin_ = nullptr;
    QCheckBox* invertedCheck_ = nullptr;
    QFutureWatcher<AnalysisTaskResult> watcher_;
    std::optional<AnalysisTaskResult> pendingRequest_;
    quint64 nextRequestId_ = 0;
    quint64 latestRequestedId_ = 0;
    quint64 acceptedRequestId_ = 0;
    BinarySettings acceptedSettings_;
    quint64 acceptedRevision_ = 0;
    bool running_ = false;
    bool previewActive_ = false;
    bool panelStateInitialized_ = false;
};

} // namespace xiaoyv::tools
