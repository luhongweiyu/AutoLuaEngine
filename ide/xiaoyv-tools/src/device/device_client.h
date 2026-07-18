/**
 * 文件用途：声明设备业务客户端，只负责截图、投影、脚本会话和操作状态。
 */
#pragma once

#include "device/device_transport.h"

#include <QImage>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QTimer>

#include <functional>

namespace xiaoyv::tools {

class DeviceClient final : public QObject {
    Q_OBJECT

public:
    enum class ConnectionState {
        Unknown,
        Checking,
        Connected,
        Disconnected,
    };
    Q_ENUM(ConnectionState)

    enum class Operation {
        Connection,
        Screenshot,
        Projection,
        Script,
        ScriptStop,
    };

    explicit DeviceClient(QObject* parent = nullptr);

    DeviceConnectionSettings settings() const;
    void setSettings(const DeviceConnectionSettings& settings);
    void saveSettings() const;
    void applyCommandLine(const QStringList& arguments);
    bool isBusy(Operation operation) const;
    bool isScriptRunning() const;
    ConnectionState connectionState() const;
    QString connectionMessage() const;

public slots:
    void checkConnection();
    void requestScreenshot();
    void projectImage(const QImage& image);
    void runScript(const QString& language, const QString& code);
    void stopScript();

signals:
    void connectionStateChanged(ConnectionState state, const QString& message);
    void screenshotReceived(const QImage& image);
    void projectionOpened();
    void scriptStarted();
    void scriptLogsReceived(const QStringList& logs);
    void scriptFinished(const QString& summary, const QStringList& allLogs);
    void stateChanged();
    void requestFailed(const QString& action, const QString& message);

private:
    using JsonCallback = DeviceTransport::JsonCallback;
    using ErrorCallback = DeviceTransport::ErrorCallback;

    void callJsonRpc(
            const QString& method,
            const QJsonObject& params,
            const QString& action,
            JsonCallback success,
            int timeoutMs = 10000,
            ErrorCallback failed = {},
            bool quietFailure = false);
    void reportFailure(
            const QString& action,
            const QString& message,
            const ErrorCallback& failed,
            bool quietFailure);
    void pollScriptLogs();
    void consumeLogResult(const QJsonValue& value);
    void finishScript(quint64 token, const QString& summary);
    void completeScript(quint64 token, const QString& summary);
    void setConnectionState(ConnectionState state, const QString& message);
    void setBusy(Operation operation, bool busy);

    DeviceTransport transport_;
    QTimer logTimer_;
    QSet<Operation> busyOperations_;
    qint64 logAfterId_ = 0;
    bool logPollInFlight_ = false;
    bool scriptRunning_ = false;
    quint64 scriptToken_ = 0;
    QStringList accumulatedLogs_;
    ConnectionState connectionState_ = ConnectionState::Unknown;
    QString connectionMessage_ = QString::fromUtf8("等待连接");
};

} // namespace xiaoyv::tools
