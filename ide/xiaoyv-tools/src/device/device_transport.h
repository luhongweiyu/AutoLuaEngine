/**
 * 文件用途：声明设备传输层，只负责连接设置、ADB 动态转发、HTTP 和 JSON-RPC。
 */
#pragma once

#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QObject>
#include <QProcess>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVector>

#include <functional>

namespace xiaoyv::tools {

struct DeviceConnectionSettings {
    enum class Mode {
        Lan,
        Adb,
    };

    Mode mode = Mode::Lan;
    QString host = QStringLiteral("127.0.0.1");
    int port = 18380;
    QString adbPath = QStringLiteral("D:/soft/QtScrcpy/adb.exe");
    QString serial;
    int remotePort = 18380;
};

class DeviceTransport final : public QObject {
    Q_OBJECT

public:
    using BytesCallback = std::function<void(const QByteArray&)>;
    using JsonCallback = std::function<void(const QJsonValue&)>;
    using ErrorCallback = std::function<void(const QString&)>;

    explicit DeviceTransport(QObject* parent = nullptr);
    ~DeviceTransport() override;

    DeviceConnectionSettings settings() const;
    void setSettings(const DeviceConnectionSettings& settings);
    void saveSettings() const;
    void applyCommandLine(const QStringList& arguments);

    void get(
            const QString& path,
            int timeoutMs,
            BytesCallback success,
            ErrorCallback failed);
    void put(
            const QString& path,
            const QByteArray& contentType,
            QByteArray body,
            int timeoutMs,
            BytesCallback success,
            ErrorCallback failed);
    void callJsonRpc(
            const QString& method,
            const QJsonObject& params,
            int timeoutMs,
            JsonCallback success,
            ErrorCallback failed);

signals:
    /** 任一 HTTP 请求收到有效响应时发出，不包含 JSON-RPC 业务结果判断。 */
    void transportAvailable();
    /** 端点建立或 HTTP 传输失败时发出。 */
    void transportUnavailable(const QString& message);

private:
    enum class HttpMethod {
        Get,
        Put,
        Post,
    };

    using ReadyCallback = std::function<void()>;

    struct PendingEndpointRequest {
        ReadyCallback ready;
        ErrorCallback failed;
    };

    void send(
            HttpMethod method,
            const QString& path,
            const QByteArray& contentType,
            QByteArray body,
            int timeoutMs,
            BytesCallback success,
            ErrorCallback failed);
    void loadSettings();
    void ensureEndpointAsync(ReadyCallback ready, ErrorCallback failed);
    void finishAdbForward(int exitCode, QProcess::ExitStatus status);
    void handleAdbProcessError(QProcess::ProcessError error);
    void invalidateAdbForward();
    void releaseAdbForwards();
    QNetworkRequest requestFor(const QString& path, int timeoutMs) const;
    QUrl endpoint(const QString& path) const;

    DeviceConnectionSettings settings_;
    QNetworkAccessManager network_;
    QProcess adbForwardProcess_;
    QTimer adbForwardTimer_;
    QVector<PendingEndpointRequest> pendingEndpointRequests_;
    QSet<int> adbForwardPorts_;
    int localAdbPort_ = 0;
    int nextRpcId_ = 1;
};

} // namespace xiaoyv::tools
