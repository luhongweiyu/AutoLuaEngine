/**
 * 文件用途：实现稳定布局的颜色/灰度二值化，并串行执行大图预览任务。
 */
#include "panels/analysis_panel.h"

#include "model/color_point_model.h"
#include "model/image_document.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSlider>
#include <QSpinBox>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <QtConcurrent>

namespace xiaoyv::tools {

AnalysisPanel::AnalysisPanel(QWidget* parent)
        : QWidget(parent) {
    colorMode_ = new QRadioButton(QString::fromUtf8("颜色规则"), this);
    grayMode_ = new QRadioButton(QString::fromUtf8("灰度阈值"), this);
    colorMode_->setChecked(true);
    auto* modeGroup = new QButtonGroup(this);
    modeGroup->addButton(colorMode_);
    modeGroup->addButton(grayMode_);

    colorRulesEdit_ = new QLineEdit(this);
    colorRulesEdit_->setObjectName(QStringLiteral("binaryColorRulesInput"));
    colorRulesEdit_->setPlaceholderText(QStringLiteral("D9F9FF-505050|B9B1DA-000000"));
    colorRulesEdit_->setToolTip(QString::fromUtf8(
            "颜色规则：RRGGBB-偏色，多条用 | 分隔；取色点变化时会同步，也可手动修改"));
    graySlider_ = new QSlider(Qt::Horizontal, this);
    graySlider_->setRange(0, 255);
    graySlider_->setValue(128);
    graySpin_ = new QSpinBox(this);
    graySpin_->setRange(0, 255);
    graySpin_->setValue(128);
    auto* grayContainer = new QWidget(this);
    auto* grayLayout = new QHBoxLayout(grayContainer);
    grayLayout->setContentsMargins(0, 0, 0, 0);
    grayLayout->setSpacing(4);
    grayLayout->addWidget(graySlider_, 1);
    grayLayout->addWidget(graySpin_);

    invertedCheck_ = new QCheckBox(QString::fromUtf8("反色"), this);
    auto* applyButton = new QPushButton(QString::fromUtf8("应用"), this);
    applyButton->setObjectName(QStringLiteral("analysisApplyButton"));

    auto* modes = new QHBoxLayout();
    modes->setContentsMargins(0, 0, 0, 0);
    modes->addWidget(colorMode_);
    modes->addWidget(grayMode_);
    modes->addStretch();

    auto* form = new QFormLayout();
    form->setContentsMargins(0, 0, 0, 0);
    form->addRow(QString::fromUtf8("颜色"), colorRulesEdit_);
    form->addRow(QString::fromUtf8("灰度阈值"), grayContainer);

    auto* actions = new QHBoxLayout();
    actions->setContentsMargins(0, 0, 0, 0);
    actions->addWidget(invertedCheck_);
    actions->addStretch();
    actions->addWidget(applyButton);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(6);
    layout->addLayout(modes);
    layout->addLayout(form);
    layout->addLayout(actions);
    layout->addStretch();

    connect(colorMode_, &QRadioButton::toggled, this, [this](bool checked) {
        if (!checked) return;
        updateModeControls();
        settingsChanged();
    });
    connect(grayMode_, &QRadioButton::toggled, this, [this](bool checked) {
        if (!checked) return;
        updateModeControls();
        settingsChanged();
    });
    connect(colorRulesEdit_, &QLineEdit::editingFinished,
            this, &AnalysisPanel::settingsChanged);
    connect(graySlider_, &QSlider::valueChanged, graySpin_, &QSpinBox::setValue);
    connect(graySpin_, &QSpinBox::valueChanged, graySlider_, &QSlider::setValue);
    connect(graySpin_, &QSpinBox::valueChanged, this, &AnalysisPanel::settingsChanged);
    connect(invertedCheck_, &QCheckBox::toggled, this, &AnalysisPanel::settingsChanged);
    connect(applyButton, &QPushButton::clicked, this, &AnalysisPanel::applyPreview);
    connect(&watcher_, &QFutureWatcher<AnalysisTaskResult>::finished,
            this, &AnalysisPanel::handleFinishedRequest);
    updateModeControls();
}

AnalysisPanel::~AnalysisPanel() {
    watcher_.waitForFinished();
}

void AnalysisPanel::setDocument(ImageDocument* document) {
    if (document_ == document) return;
    disconnect(colorPointsConnection_);
    invalidatePreview(true);
    document_ = document;
    if (document_ != nullptr) {
        if (!panelStateInitialized_) {
            const QSignalBlocker blocker(colorRulesEdit_);
            colorRulesEdit_->setText(document_->colorPoints()->enabledColorRulesText());
            panelStateInitialized_ = true;
        }
        colorPointsConnection_ = connect(
                document_->colorPoints(), &ColorPointModel::pointsChanged,
                this, &AnalysisPanel::syncColorRulesFromDocument);
    }
    settingsChanged();
}

void AnalysisPanel::setPreviewActive(bool active) {
    if (previewActive_ == active) return;
    previewActive_ = active;
    if (!previewActive_) {
        invalidatePreview(true);
        return;
    }
    settingsChanged();
}

AnalysisTaskResult AnalysisPanel::makeRequest(bool applyWhenReady) {
    AnalysisTaskResult request;
    request.requestId = ++nextRequestId_;
    request.document = document_;
    request.sourceRevision = document_ == nullptr ? 0 : document_->imageRevision();
    request.settings = currentSettings();
    request.applyWhenReady = applyWhenReady;
    return request;
}

BinarySettings AnalysisPanel::currentSettings() const {
    BinarySettings settings;
    settings.mode = colorMode_->isChecked() ? BinaryMode::Color : BinaryMode::Grayscale;
    settings.colorRules = colorRulesEdit_->text().trimmed();
    settings.grayscaleThreshold = graySpin_->value();
    settings.inverted = invertedCheck_->isChecked();
    return settings;
}

void AnalysisPanel::syncColorRulesFromDocument() {
    const QString rules = document_ == nullptr
            ? QString{}
            : document_->colorPoints()->enabledColorRulesText();
    {
        // 取色点同步和手动编辑共用同一个输入框；阻断文本信号后只启动一次预览任务。
        const QSignalBlocker blocker(colorRulesEdit_);
        colorRulesEdit_->setText(rules);
    }
    settingsChanged();
}

void AnalysisPanel::settingsChanged() {
    invalidatePreview(true);
    if (!previewActive_ || document_ == nullptr) return;
    if (colorMode_->isChecked() && colorRulesEdit_->text().trimmed().isEmpty()) return;
    requestPreview(false);
}

void AnalysisPanel::updateModeControls() {
    const bool color = colorMode_->isChecked();
    colorRulesEdit_->setEnabled(color);
    graySlider_->setEnabled(!color);
    graySpin_->setEnabled(!color);
}

void AnalysisPanel::requestPreview(bool applyWhenReady) {
    if (document_ == nullptr || document_->image().isNull()) {
        emit statusMessage(QString::fromUtf8("请先打开图片"));
        return;
    }
    AnalysisTaskResult request = makeRequest(applyWhenReady);
    latestRequestedId_ = request.requestId;
    pendingRequest_ = std::move(request);
    startPendingRequest();
}

void AnalysisPanel::startPendingRequest() {
    if (running_ || !pendingRequest_.has_value()) return;
    AnalysisTaskResult request = std::move(*pendingRequest_);
    pendingRequest_.reset();
    const QImage source = request.document == nullptr ? QImage{} : request.document->image();
    running_ = true;
    watcher_.setFuture(QtConcurrent::run([request = std::move(request), source]() mutable {
        makeBinaryPreview(source, request.settings, &request.preview, &request.error);
        return request;
    }));
}

void AnalysisPanel::handleFinishedRequest() {
    running_ = false;
    AnalysisTaskResult result = watcher_.result();
    const bool current = result.requestId == latestRequestedId_
            && result.document != nullptr
            && result.document == document_
            && result.document->imageRevision() == result.sourceRevision
            && result.settings == currentSettings();
    if (current) {
        if (!result.error.isEmpty()) {
            emit statusMessage(QString::fromUtf8("二值化失败：") + result.error);
        } else if (result.document->setPreview(std::move(result.preview), result.sourceRevision)) {
            acceptedRequestId_ = result.requestId;
            acceptedRevision_ = result.sourceRevision;
            acceptedSettings_ = result.settings;
            if (result.applyWhenReady) {
                result.document->applyPreview();
                acceptedRequestId_ = 0;
            }
        }
    }
    startPendingRequest();
}

void AnalysisPanel::invalidatePreview(bool clearVisiblePreview) {
    latestRequestedId_ = ++nextRequestId_;
    acceptedRequestId_ = 0;
    acceptedRevision_ = 0;
    pendingRequest_.reset();
    if (clearVisiblePreview && document_ != nullptr) document_->clearPreview();
}

void AnalysisPanel::applyPreview() {
    if (!previewActive_) return;
    if (document_ == nullptr) {
        emit statusMessage(QString::fromUtf8("请先打开图片"));
        return;
    }
    const bool accepted = acceptedRequestId_ != 0
            && acceptedRequestId_ == latestRequestedId_
            && acceptedRevision_ == document_->imageRevision()
            && acceptedSettings_ == currentSettings()
            && document_->previewMatches(acceptedRevision_);
    if (accepted) {
        document_->applyPreview();
        acceptedRequestId_ = 0;
        return;
    }
    requestPreview(true);
}

} // namespace xiaoyv::tools
