/**
 * 文件用途：实现代码复制、运行和停止交互，不在主窗口保存第二份代码状态。
 */
#include "panels/script_editor_panel.h"

#include <QApplication>
#include <QClipboard>
#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace xiaoyv::tools {

ScriptEditorPanel::ScriptEditorPanel(QWidget* parent)
        : QWidget(parent) {
    editor_ = new QPlainTextEdit(this);
    editor_->setObjectName(QStringLiteral("generatedCodeEditor"));
    editor_->setPlaceholderText(QString::fromUtf8("生成的 Lua 或 JavaScript 代码"));
    auto* copyButton = new QPushButton(QString::fromUtf8("复制"), this);
    runButton_ = new QPushButton(QString::fromUtf8("运行当前代码"), this);
    stopButton_ = new QPushButton(QString::fromUtf8("停止"), this);
    stopButton_->setEnabled(false);

    auto* actions = new QHBoxLayout();
    actions->setContentsMargins(0, 0, 0, 0);
    actions->setSpacing(3);
    actions->addWidget(copyButton);
    actions->addWidget(runButton_);
    actions->addWidget(stopButton_);
    actions->addStretch();
    auto* layout = new QVBoxLayout(this);
    // 停靠窗已经提供外边框，内容必须贴合边界，避免嵌入后出现上下左右的空占位。
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);
    layout->addLayout(actions);
    layout->addWidget(editor_);

    connect(copyButton, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(editor_->toPlainText());
        emit statusMessage(QString::fromUtf8("代码已复制"));
    });
    connect(runButton_, &QPushButton::clicked, this, [this] {
        emit runRequested(language_, editor_->toPlainText());
    });
    connect(stopButton_, &QPushButton::clicked, this, &ScriptEditorPanel::stopRequested);
}

void ScriptEditorPanel::setCode(const QString& language, const QString& code) {
    language_ = language.trimmed().toLower();
    if (language_.isEmpty()) language_ = QStringLiteral("lua");
    editor_->setPlainText(code);
}

void ScriptEditorPanel::setExecutionState(bool running, bool starting, bool stopping) {
    runButton_->setEnabled(!running && !starting);
    stopButton_->setEnabled(running && !stopping);
}

} // namespace xiaoyv::tools
