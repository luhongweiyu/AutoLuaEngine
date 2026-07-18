/**
 * 文件用途：实现单一设备传输通道，业务层不接触 ADB 进程和网络响应细节。
 */
#include "device/device_transport.h"

#include <QFileInfo>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QSettings>

#include <utility>

namespace xiaoyv::tools {
namespace {

QString responseError(QNetworkReply* reply, const QByteArray& body) {
    QString message = reply->errorString();
    const QJsonDocument json = QJsonDocument::fromJson(body);
    if (json.isObject()) {
        const QString serverMessage = json.object().value(QStringLiteral("error")).toString();
        if (!serverMessage.isEmpty()) message += QString::fromUtf8("：") + serverMessage;
    } else if (!body.trimmed().isEmpty()) {
        message += QString::fromUtf8("：") + QString::fromUtf8(body).trimmed();
    }
    return message;
}

} // namespace

DeviceTransport::DeviceTransport(QObject* parent)
        : QObject(parent), network_(this) {
    adbForwardTimer_.setSingleShot(true);
    adbForwardTimer_.setInterval(10000);
    connect(&adbForwardProcess_, &QProcess::finished,
            this, &DeviceTransport::finishAdbForward);
    connect(&adbForwardProcess_, &QProcess::errorOccurred,
            this, &DeviceTransport::handleAdbProcessError);
    connect(&adbForwardTimer_, &QTimer::timeout, this, [this] {
        const QVector<PendingEndpointRequest> pending = std::exchange(pendingEndpointRequests_, {});
        if (adbForwardProcess_.state() != QProcess::NotRunning) adbForwardProcess_.kill();
        for (const PendingEndpointRequest& request : pending) {
            request.failed(QString::fromUtf8("建立 ADB 动态端口超时"));
        }
    });
    loadSettings();
}

DeviceTransport::~DeviceTransport() {
    releaseAdbForwards();
}

DeviceConnectionSettings DeviceTransport::settings() const {
    return settings_;
}

void DeviceTransport::setSettings(const DeviceConnectionSettings& settings) {
    const bool endpointChanged = settings_.mode != settings.mode
            || settings_.adbPath != settings.adbPath
            || settings_.serial != settings.serial
            || settings_.remotePort != settings.remotePort;
    if (endpointChanged) {
        adbForwardTimer_.stop();
        if (adbForwardProcess_.state() != QProcess::NotRunning) {
            adbForwardProcess_.kill();
            adbForwardProcess_.waitForFinished(1000);
        }
        const QVector<PendingEndpointRequest> pending = std::exchange(pendingEndpointRequests_, {});
        for (const PendingEndpointRequest& request : pending) {
            request.failed(QString::fromUtf8("连接设置已改变，请重新执行操作"));
        }
        releaseAdbForwards();
    }
    settings_ = settings;
}

void DeviceTransport::loadSettings() {
    QSettings saved;
    DeviceConnectionSettings loaded;
    loaded.mode = saved.value(QStringLiteral("connection/mode"), QStringLiteral("lan")).toString()
                          == QLatin1String("adb")
            ? DeviceConnectionSettings::Mode::Adb
            : DeviceConnectionSettings::Mode::Lan;
    loaded.host = saved.value(QStringLiteral("connection/host"), loaded.host).toString();
    loaded.port = saved.value(QStringLiteral("connection/port"), loaded.port).toInt();
    loaded.adbPath = saved.value(QStringLiteral("connection/adbPath"), loaded.adbPath).toString();
    loaded.serial = saved.value(QStringLiteral("connection/serial")).toString();
    loaded.remotePort = saved.value(QStringLiteral("connection/remotePort"), loaded.remotePort).toInt();
    setSettings(loaded);
}

void DeviceTransport::saveSettings() const {
    QSettings saved;
    saved.setValue(
            QStringLiteral("connection/mode"),
            settings_.mode == DeviceConnectionSettings::Mode::Adb
                    ? QStringLiteral("adb")
                    : QStringLiteral("lan"));
    saved.setValue(QStringLiteral("connection/host"), settings_.host);
    saved.setValue(QStringLiteral("connection/port"), settings_.port);
    saved.setValue(QStringLiteral("connection/adbPath"), settings_.adbPath);
    saved.setValue(QStringLiteral("connection/serial"), settings_.serial);
    saved.setValue(QStringLiteral("connection/remotePort"), settings_.remotePort);
}

void DeviceTransport::applyCommandLine(const QStringList& arguments) {
    DeviceConnectionSettings updated = settings_;
    for (int index = 1; index < arguments.size(); ++index) {
        const QString argument = arguments[index];
        auto takeNext = [&]() -> QString {
            return index + 1 < arguments.size() ? arguments[++index] : QString{};
        };
        if (argument == QLatin1String("--connection-mode")) {
            updated.mode = takeNext().compare(QStringLiteral("adb"), Qt::CaseInsensitive) == 0
                    ? DeviceConnectionSettings::Mode::Adb
                    : DeviceConnectionSettings::Mode::Lan;
        } else if (argument == QLatin1String("--host")) {
            updated.host = takeNext();
        } else if (argument == QLatin1String("--port")) {
            updated.port = takeNext().toInt();
        } else if (argument == QLatin1String("--adb")) {
            updated.adbPath = takeNext();
        } else if (argument == QLatin1String("--device")) {
            updated.serial = takeNext();
        } else if (argument == QLatin1String("--remote-port")) {
            updated.remotePort = takeNext().toInt();
        }
    }
    setSettings(updated);
}

void DeviceTransport::get(
        const QString& path,
        int timeoutMs,
        BytesCallback success,
        ErrorCallback failed) {
    send(HttpMethod::Get, path, {}, {}, timeoutMs, std::move(success), std::move(failed));
}

void DeviceTransport::put(
        const QString& path,
        const QByteArray& contentType,
        QByteArray body,
        int timeoutMs,
        BytesCallback success,
        ErrorCallback failed) {
    send(HttpMethod::Put, path, contentType, std::move(body), timeoutMs,
         std::move(success), std::move(failed));
}

void DeviceTransport::callJsonRpc(
        const QString& method,
        const QJsonObject& params,
        int timeoutMs,
        JsonCallback success,
        ErrorCallback failed) {
    const int requestId = nextRpcId_++;
    const QJsonObject call{
            {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
            {QStringLiteral("id"), requestId},
            {QStringLiteral("method"), method},
            {QStringLiteral("params"), params},
    };
    const ErrorCallback protocolFailed = failed;
    send(
            HttpMethod::Post,
            QStringLiteral("/jsonrpc"),
            QByteArrayLiteral("application/json; charset=utf-8"),
            QJsonDocument(call).toJson(QJsonDocument::Compact),
            timeoutMs,
            [requestId, success = std::move(success), protocolFailed](const QByteArray& body) {
                QJsonParseError parseError;
                const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
                if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
                    if (protocolFailed) {
                        protocolFailed(QString::fromUtf8("设备返回的 JSON-RPC 响应无效"));
                    }
                    return;
                }
                const QJsonObject response = document.object();
                if (response.value(QStringLiteral("id")).toInt(-1) != requestId) {
                    if (protocolFailed) {
                        protocolFailed(QString::fromUtf8("设备返回了不匹配的请求编号"));
                    }
                } else if (response.contains(QStringLiteral("error"))) {
                    if (protocolFailed) {
                        protocolFailed(response.value(QStringLiteral("error")).toObject()
                                .value(QStringLiteral("message"))
                                .toString(QString::fromUtf8("未知错误")));
                    }
                } else if (success) {
                    success(response.value(QStringLiteral("result")));
                }
            },
            std::move(failed));
}

void DeviceTransport::send(
        HttpMethod method,
        const QString& path,
        const QByteArray& contentType,
        QByteArray body,
        int timeoutMs,
        BytesCallback success,
        ErrorCallback failed) {
    // 失败回调允许省略；传输层内部会跨同步校验和异步响应多次转交该回调。
    if (!failed) failed = [](const QString&) {};
    const ErrorCallback endpointFailed = [this, failed](const QString& error) {
        emit transportUnavailable(error);
        failed(error);
    };
    ensureEndpointAsync(
            [this, method, path, contentType, body = std::move(body), timeoutMs,
             success = std::move(success), failed]() mutable {
                QNetworkRequest request = requestFor(path, timeoutMs);
                if (!contentType.isEmpty()) {
                    request.setHeader(QNetworkRequest::ContentTypeHeader, contentType);
                }
                QNetworkReply* reply = nullptr;
                switch (method) {
                    case HttpMethod::Get:
                        reply = network_.get(request);
                        break;
                    case HttpMethod::Put:
                        reply = network_.put(request, body);
                        break;
                    case HttpMethod::Post:
                        reply = network_.post(request, body);
                        break;
                }
                connect(reply, &QNetworkReply::finished, this,
                        [this, reply, success = std::move(success), failed] {
                            const QByteArray response = reply->readAll();
                            // 收到 HTTP 状态码即可证明设备服务可达；4xx/5xx 属于请求失败，
                            // 不能误报为设备断线，也不应销毁仍然有效的 ADB 转发。
                            const bool endpointReached = reply->attribute(
                                    QNetworkRequest::HttpStatusCodeAttribute).isValid();
                            if (endpointReached) emit transportAvailable();
                            if (reply->error() == QNetworkReply::NoError) {
                                if (success) success(response);
                            } else {
                                const QString error = responseError(reply, response);
                                if (!endpointReached) {
                                    invalidateAdbForward();
                                    emit transportUnavailable(error);
                                }
                                if (failed) failed(error);
                            }
                            reply->deleteLater();
                        });
            },
            endpointFailed);
}

void DeviceTransport::ensureEndpointAsync(ReadyCallback ready, ErrorCallback failed) {
    if (settings_.mode == DeviceConnectionSettings::Mode::Lan) {
        if (settings_.host.trimmed().isEmpty() || settings_.port <= 0 || settings_.port > 65535) {
            failed(QString::fromUtf8("局域网 IP 或端口无效"));
        } else {
            ready();
        }
        return;
    }
    if (localAdbPort_ > 0) {
        ready();
        return;
    }
    if (settings_.remotePort <= 0 || settings_.remotePort > 65535) {
        failed(QString::fromUtf8("ADB 引擎端口无效"));
        return;
    }
    pendingEndpointRequests_.push_back({std::move(ready), std::move(failed)});
    if (adbForwardProcess_.state() != QProcess::NotRunning) return;
    if (!QFileInfo(settings_.adbPath).isFile()) {
        const QVector<PendingEndpointRequest> pending = std::exchange(pendingEndpointRequests_, {});
        for (const PendingEndpointRequest& request : pending) {
            request.failed(QString::fromUtf8("ADB 路径无效：") + settings_.adbPath);
        }
        return;
    }
    QStringList arguments;
    if (!settings_.serial.trimmed().isEmpty()) {
        arguments << QStringLiteral("-s") << settings_.serial.trimmed();
    }
    arguments << QStringLiteral("forward") << QStringLiteral("tcp:0")
              << QStringLiteral("tcp:%1").arg(settings_.remotePort);
    adbForwardProcess_.start(settings_.adbPath, arguments);
    adbForwardTimer_.start();
}

void DeviceTransport::finishAdbForward(int exitCode, QProcess::ExitStatus status) {
    adbForwardTimer_.stop();
    const QVector<PendingEndpointRequest> pending = std::exchange(pendingEndpointRequests_, {});
    bool ok = false;
    const int port = QString::fromUtf8(adbForwardProcess_.readAllStandardOutput()).trimmed().toInt(&ok);
    if (status == QProcess::NormalExit && exitCode == 0 && ok && port > 0 && port <= 65535) {
        localAdbPort_ = port;
        adbForwardPorts_.insert(port);
        for (const PendingEndpointRequest& request : pending) request.ready();
        return;
    }
    QString message = QString::fromUtf8(adbForwardProcess_.readAllStandardError()).trimmed();
    if (message.isEmpty()) message = QString::fromUtf8("ADB 没有返回有效动态端口");
    for (const PendingEndpointRequest& request : pending) request.failed(message);
}

void DeviceTransport::handleAdbProcessError(QProcess::ProcessError error) {
    if (error != QProcess::FailedToStart || pendingEndpointRequests_.isEmpty()) return;
    adbForwardTimer_.stop();
    const QVector<PendingEndpointRequest> pending = std::exchange(pendingEndpointRequests_, {});
    const QString message = QString::fromUtf8("无法启动 ADB：") + adbForwardProcess_.errorString();
    for (const PendingEndpointRequest& request : pending) request.failed(message);
}

void DeviceTransport::invalidateAdbForward() {
    if (settings_.mode == DeviceConnectionSettings::Mode::Adb) localAdbPort_ = 0;
}

void DeviceTransport::releaseAdbForwards() {
    adbForwardTimer_.stop();
    if (adbForwardProcess_.state() != QProcess::NotRunning) {
        adbForwardProcess_.kill();
        adbForwardProcess_.waitForFinished(1000);
    }
    const QSet<int> ports = std::exchange(adbForwardPorts_, {});
    if (!settings_.adbPath.isEmpty()) {
        for (int port : ports) {
            QStringList arguments;
            if (!settings_.serial.trimmed().isEmpty()) {
                arguments << QStringLiteral("-s") << settings_.serial.trimmed();
            }
            arguments << QStringLiteral("forward") << QStringLiteral("--remove")
                      << QStringLiteral("tcp:%1").arg(port);
            QProcess process;
            process.start(settings_.adbPath, arguments);
            process.waitForFinished(1000);
        }
    }
    localAdbPort_ = 0;
}

QNetworkRequest DeviceTransport::requestFor(const QString& path, int timeoutMs) const {
    QNetworkRequest request(endpoint(path));
    if (timeoutMs > 0) request.setTransferTimeout(timeoutMs);
    return request;
}

QUrl DeviceTransport::endpoint(const QString& path) const {
    QUrl result;
    result.setScheme(QStringLiteral("http"));
    result.setHost(settings_.mode == DeviceConnectionSettings::Mode::Adb
                           ? QStringLiteral("127.0.0.1")
                           : settings_.host.trimmed());
    result.setPort(settings_.mode == DeviceConnectionSettings::Mode::Adb
                           ? localAdbPort_
                           : settings_.port);
    result.setPath(path);
    return result;
}

} // namespace xiaoyv::tools
