/**
 * 文件用途：声明生成代码编辑与运行控制面板，独占当前代码语言和按钮状态。
 */
#pragma once

#include <QWidget>

class QPlainTextEdit;
class QPushButton;

namespace xiaoyv::tools {

class ScriptEditorPanel final : public QWidget {
    Q_OBJECT

public:
    explicit ScriptEditorPanel(QWidget* parent = nullptr);

    void setCode(const QString& language, const QString& code);
    /** 根据设备会话状态统一刷新运行和停止按钮。 */
    void setExecutionState(bool running, bool starting, bool stopping);

signals:
    void runRequested(const QString& language, const QString& code);
    void stopRequested();
    void statusMessage(const QString& message);

private:
    QPlainTextEdit* editor_ = nullptr;
    QPushButton* runButton_ = nullptr;
    QPushButton* stopButton_ = nullptr;
    QString language_ = QStringLiteral("lua");
};

} // namespace xiaoyv::tools
