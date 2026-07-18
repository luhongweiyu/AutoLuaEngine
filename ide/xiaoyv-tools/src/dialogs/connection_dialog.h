/**
 * 文件用途：声明设备连接设置对话框，LAN 与 ADB 参数分别使用固定表单。
 */
#pragma once

#include <QDialog>

#include "device/device_transport.h"

class QComboBox;
class QLineEdit;
class QSpinBox;
class QWidget;

namespace xiaoyv::tools {

class ConnectionDialog final : public QDialog {
    Q_OBJECT

public:
    explicit ConnectionDialog(const DeviceConnectionSettings& settings, QWidget* parent = nullptr);
    DeviceConnectionSettings value() const;

private:
    void updateModePage();

    QComboBox* modeCombo_ = nullptr;
    QWidget* lanPage_ = nullptr;
    QWidget* adbPage_ = nullptr;
    QLineEdit* hostEdit_ = nullptr;
    QSpinBox* portSpin_ = nullptr;
    QLineEdit* adbPathEdit_ = nullptr;
    QLineEdit* serialEdit_ = nullptr;
    QSpinBox* remotePortSpin_ = nullptr;
};

} // namespace xiaoyv::tools
