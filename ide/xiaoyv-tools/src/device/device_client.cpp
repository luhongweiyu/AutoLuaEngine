/**
 * 文件用途：实现设备业务流程，所有网络和 ADB 细节统一委托给 DeviceTransport。
 */
#include "device/device_client.h"

#include <QBuffer>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QtConcurrent>

#include <algorithm>

namespace xiaoyv::tools {
namespace {

quint32 readLittleEndian32(const char* bytes) {
    const auto* value = reinterpret_cast<const unsigned char*>(bytes);
    return static_cast<quint32>(value[0])
            | (static_cast<quint32>(value[1]) << 8U)
            | (static_cast<quint32>(value[2]) << 16U)
            | (static_cast<quint32>(value[3]) << 24U);
}

QString compactJson(const QJsonValue& value) {
    if (value.isObject()) {
        return QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
    }
    if (value.isArray()) {
        return QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact));
    }
    return value.toVariant().toString();
}

} // namespace

DeviceClient::DeviceClient(QObject* parent)
        : QObject(parent), transport_(this) {
    logTimer_.setInterval(500);
    connect(&logTimer_, &QTimer::timeout, this, &DeviceClient::pollScriptLogs);
    connect(&transport_, &DeviceTransport::transportAvailable, this, [this] {
        setConnectionState(ConnectionState::Connected, QString::fromUtf8("设备已连接"));
    });
    connect(&transport_, &DeviceTransport::transportUnavailable, this,
            [this](const QString& message) {
                setConnectionState(
                        ConnectionState::Disconnected,
                        QString::fromUtf8("设备未连接：") + message);
            });
}

DeviceConnectionSettings DeviceClient::settings() const {
    return transport_.settings();
}

void DeviceClient::setSettings(const DeviceConnectionSettings& settings) {
    transport_.setSettings(settings);
    setConnectionState(ConnectionState::Unknown, QString::fromUtf8("等待连接"));
}

void DeviceClient::saveSettings() const {
    transport_.saveSettings();
}

void DeviceClient::applyCommandLine(const QStringList& arguments) {
    transport_.applyCommandLine(arguments);
    setConnectionState(ConnectionState::Unknown, QString::fromUtf8("等待连接"));
}

bool DeviceClient::isBusy(Operation operation) const {
    return busyOperations_.contains(operation);
}

bool DeviceClient::isScriptRunning() const {
    return scriptRunning_;
}

DeviceClient::ConnectionState DeviceClient::connectionState() const {
    return connectionState_;
}

QString DeviceClient::connectionMessage() const {
    return connectionMessage_;
}

void DeviceClient::checkConnection() {
    constexpr Operation operation = Operation::Connection;
    if (isBusy(operation)) return;
    setBusy(operation, true);
    setConnectionState(ConnectionState::Checking, QString::fromUtf8("正在连接设备"));
    transport_.get(
            QStringLiteral("/health"),
            5000,
            [this, operation](const QByteArray& body) {
                const QJsonDocument document = QJsonDocument::fromJson(body);
                const bool connected = document.isObject()
                        && document.object().value(QStringLiteral("ok")).toBool();
                setConnectionState(
                        connected ? ConnectionState::Connected : ConnectionState::Disconnected,
                        connected ? QString::fromUtf8("设备已连接")
                                  : QString::fromUtf8("设备服务状态异常"));
                setBusy(operation, false);
            },
            [this, operation](const QString& error) {
                setConnectionState(
                        ConnectionState::Disconnected,
                        QString::fromUtf8("设备未连接：") + error);
                setBusy(operation, false);
            });
}

void DeviceClient::requestScreenshot() {
    constexpr Operation operation = Operation::Screenshot;
    if (isBusy(operation)) return;
    setBusy(operation, true);
    transport_.get(
            QStringLiteral("/tool/screenshot"),
            15000,
            [this, operation](const QByteArray& frame) {
                if (frame.size() < 12 || frame.first(4) != QByteArrayLiteral("XYVF")) {
                    emit requestFailed(
                            QString::fromUtf8("设备截图"),
                            QString::fromUtf8("设备返回的截图帧无效"));
                } else {
                    const quint32 width = readLittleEndian32(frame.constData() + 4);
                    const quint32 height = readLittleEndian32(frame.constData() + 8);
                    const quint64 pixelCount = static_cast<quint64>(width) * height;
                    const quint64 expectedBytes = 12ULL + pixelCount * 4ULL;
                    if (width == 0 || height == 0 || width > 16384 || height > 16384
                            || expectedBytes != static_cast<quint64>(frame.size())) {
                        emit requestFailed(
                                QString::fromUtf8("设备截图"),
                                QString::fromUtf8("截图宽高或点阵长度无效"));
                    } else {
                        const QImage view(
                                reinterpret_cast<const uchar*>(frame.constData() + 12),
                                static_cast<int>(width),
                                static_cast<int>(height),
                                static_cast<int>(width * 4U),
                                QImage::Format_RGBA8888);
                        emit screenshotReceived(view.copy());
                    }
                }
                setBusy(operation, false);
            },
            [this, operation](const QString& error) {
                emit requestFailed(QString::fromUtf8("设备截图"), error);
                setBusy(operation, false);
            });
}

void DeviceClient::projectImage(const QImage& image) {
    constexpr Operation operation = Operation::Projection;
    if (isBusy(operation)) return;
    if (image.isNull()) {
        emit requestFailed(QString::fromUtf8("投影"), QString::fromUtf8("当前没有可投影的图片"));
        return;
    }
    setBusy(operation, true);
    auto* watcher = new QFutureWatcher<QByteArray>(this);
    connect(watcher, &QFutureWatcher<QByteArray>::finished, this,
            [this, watcher, operation] {
                const QByteArray encoded = watcher->result();
                watcher->deleteLater();
                if (encoded.isEmpty()) {
                    emit requestFailed(
                            QString::fromUtf8("投影"),
                            QString::fromUtf8("当前图片无法编码为 PNG"));
                    setBusy(operation, false);
                    return;
                }
                transport_.put(
                        QStringLiteral("/tool/image"),
                        QByteArrayLiteral("image/png"),
                        encoded,
                        30000,
                        [this, operation](const QByteArray& body) {
                            const QJsonDocument document = QJsonDocument::fromJson(body);
                            const QString fileId = document.isObject()
                                    ? document.object().value(QStringLiteral("fileId")).toString()
                                    : QString{};
                            if (fileId.isEmpty()) {
                                emit requestFailed(
                                        QString::fromUtf8("投影"),
                                        QString::fromUtf8("设备没有返回图片标识"));
                                setBusy(operation, false);
                                return;
                            }
                            callJsonRpc(
                                    QStringLiteral("viewer.openImage"),
                                    {{QStringLiteral("fileId"), fileId}},
                                    QString::fromUtf8("投影"),
                                    [this, operation](const QJsonValue&) {
                                        emit projectionOpened();
                                        setBusy(operation, false);
                                    },
                                    10000,
                                    [this, operation](const QString&) {
                                        setBusy(operation, false);
                                    });
                        },
                        [this, operation](const QString& error) {
                            emit requestFailed(QString::fromUtf8("投影"), error);
                            setBusy(operation, false);
                        });
            });
    watcher->setFuture(QtConcurrent::run([image] {
        QByteArray encoded;
        QBuffer buffer(&encoded);
        if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG")) return QByteArray{};
        return encoded;
    }));
}

void DeviceClient::runScript(const QString& language, const QString& code) {
    constexpr Operation operation = Operation::Script;
    if (scriptRunning_ || isBusy(operation)) return;
    if (code.trimmed().isEmpty()) {
        emit requestFailed(QString::fromUtf8("运行测试脚本"), QString::fromUtf8("当前代码为空"));
        return;
    }
    setBusy(operation, true);
    const quint64 token = ++scriptToken_;
    callJsonRpc(
            QStringLiteral("log.drain"),
            {{QStringLiteral("afterId"), 0}},
            QString::fromUtf8("读取日志位置"),
            [this, token, language, code](const QJsonValue& value) {
                if (token != scriptToken_) return;
                logAfterId_ = value.toObject().value(QStringLiteral("lastId")).toInteger();
                accumulatedLogs_.clear();
                scriptRunning_ = true;
                emit stateChanged();
                emit scriptStarted();
                logTimer_.start();
                callJsonRpc(
                        QStringLiteral("script.run"),
                        {{QStringLiteral("language"), language},
                         {QStringLiteral("code"), code}},
                        QString::fromUtf8("运行测试脚本"),
                        [this, token](const QJsonValue& result) {
                            finishScript(token, compactJson(result));
                        },
                        0,
                        [this, token](const QString& error) {
                            finishScript(token, QString::fromUtf8("脚本运行失败：") + error);
                        });
            },
            10000,
            [this, token](const QString&) {
                if (token == scriptToken_) setBusy(Operation::Script, false);
            });
}

void DeviceClient::stopScript() {
    constexpr Operation operation = Operation::ScriptStop;
    if (!scriptRunning_ || isBusy(operation)) return;
    setBusy(operation, true);
    callJsonRpc(
            QStringLiteral("script.stop"),
            {},
            QString::fromUtf8("停止测试脚本"),
            [](const QJsonValue&) {
                // 停止被接受后等待 script.run 真正结束，避免重复发送停止命令。
            },
            10000,
            [this, operation](const QString&) { setBusy(operation, false); });
}

void DeviceClient::callJsonRpc(
        const QString& method,
        const QJsonObject& params,
        const QString& action,
        JsonCallback success,
        int timeoutMs,
        ErrorCallback failed,
        bool quietFailure) {
    transport_.callJsonRpc(
            method,
            params,
            timeoutMs,
            std::move(success),
            [this, action, failed = std::move(failed), quietFailure](const QString& error) {
                reportFailure(action, error, failed, quietFailure);
            });
}

void DeviceClient::reportFailure(
        const QString& action,
        const QString& message,
        const ErrorCallback& failed,
        bool quietFailure) {
    if (!quietFailure) emit requestFailed(action, message);
    if (failed) failed(message);
}

void DeviceClient::pollScriptLogs() {
    if (!scriptRunning_ || logPollInFlight_) return;
    logPollInFlight_ = true;
    callJsonRpc(
            QStringLiteral("log.drain"),
            {{QStringLiteral("afterId"), logAfterId_}},
            QString::fromUtf8("读取测试日志"),
            [this](const QJsonValue& value) {
                logPollInFlight_ = false;
                consumeLogResult(value);
            },
            5000,
            [this](const QString&) { logPollInFlight_ = false; },
            true);
}

void DeviceClient::consumeLogResult(const QJsonValue& value) {
    const QJsonObject object = value.toObject();
    QStringList received;
    for (const QJsonValue& entry : object.value(QStringLiteral("entries")).toArray()) {
        const QString message = entry.toObject().value(QStringLiteral("message")).toString();
        if (!message.isEmpty()) received.push_back(message);
    }
    logAfterId_ = std::max(
            logAfterId_, object.value(QStringLiteral("lastId")).toInteger(logAfterId_));
    if (!received.isEmpty()) {
        accumulatedLogs_.append(received);
        emit scriptLogsReceived(received);
    }
}

void DeviceClient::finishScript(quint64 token, const QString& summary) {
    if (token != scriptToken_) return;
    logTimer_.stop();
    callJsonRpc(
            QStringLiteral("log.drain"),
            {{QStringLiteral("afterId"), logAfterId_}},
            QString::fromUtf8("读取测试日志"),
            [this, token, summary](const QJsonValue& value) {
                consumeLogResult(value);
                completeScript(token, summary);
            },
            5000,
            [this, token, summary](const QString&) {
                completeScript(token, summary);
            },
            true);
}

void DeviceClient::completeScript(quint64 token, const QString& summary) {
    if (token != scriptToken_) return;
    scriptRunning_ = false;
    logPollInFlight_ = false;
    emit scriptFinished(summary, accumulatedLogs_);
    setBusy(Operation::ScriptStop, false);
    setBusy(Operation::Script, false);
}

void DeviceClient::setConnectionState(ConnectionState state, const QString& message) {
    if (connectionState_ == state && connectionMessage_ == message) return;
    connectionState_ = state;
    connectionMessage_ = message;
    emit connectionStateChanged(connectionState_, connectionMessage_);
}

void DeviceClient::setBusy(Operation operation, bool busy) {
    const bool wasBusy = busyOperations_.contains(operation);
    if (busy) busyOperations_.insert(operation);
    else busyOperations_.remove(operation);
    if (wasBusy != busy) emit stateChanged();
}

} // namespace xiaoyv::tools
