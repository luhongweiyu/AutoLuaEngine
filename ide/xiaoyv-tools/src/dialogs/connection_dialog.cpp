/**
 * 文件用途：实现紧凑的 LAN/ADB 连接设置界面，确认与取消按钮使用中文。
 */
#include "dialogs/connection_dialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace xiaoyv::tools {

ConnectionDialog::ConnectionDialog(
        const DeviceConnectionSettings& settings,
        QWidget* parent)
        : QDialog(parent) {
    setWindowTitle(QString::fromUtf8("连接设置"));
    setModal(true);
    resize(430, 230);

    modeCombo_ = new QComboBox(this);
    modeCombo_->addItem(QString::fromUtf8("局域网直连"), static_cast<int>(DeviceConnectionSettings::Mode::Lan));
    modeCombo_->addItem(QStringLiteral("ADB"), static_cast<int>(DeviceConnectionSettings::Mode::Adb));
    modeCombo_->setCurrentIndex(settings.mode == DeviceConnectionSettings::Mode::Lan ? 0 : 1);

    hostEdit_ = new QLineEdit(settings.host, this);
    portSpin_ = new QSpinBox(this);
    portSpin_->setRange(1, 65535);
    portSpin_->setValue(settings.port);
    lanPage_ = new QWidget(this);
    auto* lanForm = new QFormLayout(lanPage_);
    lanForm->setContentsMargins(0, 0, 0, 0);
    lanForm->addRow(QString::fromUtf8("设备 IP"), hostEdit_);
    lanForm->addRow(QString::fromUtf8("端口"), portSpin_);

    adbPathEdit_ = new QLineEdit(settings.adbPath, this);
    serialEdit_ = new QLineEdit(settings.serial, this);
    serialEdit_->setPlaceholderText(QString::fromUtf8("留空时使用当前唯一设备"));
    remotePortSpin_ = new QSpinBox(this);
    remotePortSpin_->setRange(1, 65535);
    remotePortSpin_->setValue(settings.remotePort);
    adbPage_ = new QWidget(this);
    auto* adbForm = new QFormLayout(adbPage_);
    adbForm->setContentsMargins(0, 0, 0, 0);
    adbForm->addRow(QStringLiteral("ADB"), adbPathEdit_);
    adbForm->addRow(QString::fromUtf8("设备序列号"), serialEdit_);
    adbForm->addRow(QString::fromUtf8("引擎端口"), remotePortSpin_);

    auto* buttons = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(QString::fromUtf8("确定"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QString::fromUtf8("取消"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(modeCombo_, &QComboBox::currentIndexChanged, this, &ConnectionDialog::updateModePage);

    auto* layout = new QVBoxLayout(this);
    auto* modeForm = new QFormLayout();
    modeForm->addRow(QString::fromUtf8("连接方式"), modeCombo_);
    layout->addLayout(modeForm);
    layout->addWidget(lanPage_);
    layout->addWidget(adbPage_);
    layout->addStretch();
    layout->addWidget(buttons);
    updateModePage();
}

DeviceConnectionSettings ConnectionDialog::value() const {
    DeviceConnectionSettings result;
    result.mode = static_cast<DeviceConnectionSettings::Mode>(modeCombo_->currentData().toInt());
    result.host = hostEdit_->text().trimmed();
    result.port = portSpin_->value();
    result.adbPath = adbPathEdit_->text().trimmed();
    result.serial = serialEdit_->text().trimmed();
    result.remotePort = remotePortSpin_->value();
    return result;
}

void ConnectionDialog::updateModePage() {
    const bool lan = static_cast<DeviceConnectionSettings::Mode>(modeCombo_->currentData().toInt())
            == DeviceConnectionSettings::Mode::Lan;
    lanPage_->setVisible(lan);
    adbPage_->setVisible(!lan);
}

} // namespace xiaoyv::tools
