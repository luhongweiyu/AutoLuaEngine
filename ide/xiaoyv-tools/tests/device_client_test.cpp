/**
 * 文件用途：使用本机最小 HTTP 服务验证设备连接、原始截图帧和重复请求防抖。
 */
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>

#include "device/device_client.h"
#include "device/device_transport.h"

#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

using namespace xiaoyv::tools;

class MockDeviceServer final : public QObject {
    Q_OBJECT

public:
    explicit MockDeviceServer(QObject* parent = nullptr)
            : QObject(parent) {
        connect(&server_, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket* socket = server_.nextPendingConnection()) {
                buffers_.insert(socket, {});
                connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
                    QByteArray& buffer = buffers_[socket];
                    buffer += socket->readAll();
                    const int headerEnd = buffer.indexOf("\r\n\r\n");
                    if (headerEnd < 0) return;
                    int contentLength = 0;
                    const QList<QByteArray> headers = buffer.left(headerEnd).split('\n');
                    for (QByteArray header : headers) {
                        header = header.trimmed();
                        if (header.toLower().startsWith("content-length:")) {
                            contentLength = header.mid(header.indexOf(':') + 1).trimmed().toInt();
                        }
                    }
                    if (buffer.size() < headerEnd + 4 + contentLength) return;
                    respond(socket, buffer.left(headerEnd + 4 + contentLength));
                    buffers_.remove(socket);
                });
                connect(socket, &QObject::destroyed, this, [this, socket] { buffers_.remove(socket); });
            }
        });
    }

    bool listen() {
        return server_.listen(QHostAddress::LocalHost, 0);
    }

    quint16 port() const {
        return server_.serverPort();
    }

    int screenshotRequests() const {
        return screenshotRequests_;
    }

    int projectionUploads() const {
        return projectionUploads_;
    }

    int projectionOpens() const {
        return projectionOpens_;
    }

private:
    /** 写入一个最小 HTTP 响应；测试服务不复用连接。 */
    static void writeResponse(
            QTcpSocket* socket,
            const QByteArray& body,
            const QByteArray& contentType = "application/json",
            int status = 200) {
        if (socket == nullptr) return;
        const QByteArray response = QByteArray("HTTP/1.1 ")
                + (status == 200 ? "200 OK\r\n" : "404 Not Found\r\n")
                + "Content-Type: " + contentType + "\r\n"
                + "Content-Length: " + QByteArray::number(body.size()) + "\r\n"
                + "Connection: close\r\n\r\n" + body;
        socket->write(response);
        socket->disconnectFromHost();
    }

    /** 根据请求路径和 JSON-RPC 方法返回设备协议所需的最小结果。 */
    void respond(QTcpSocket* socket, const QByteArray& request) {
        const int headerEnd = request.indexOf("\r\n\r\n");
        const QByteArray header = request.left(headerEnd);
        const QByteArray requestBody = request.mid(headerEnd + 4);
        QByteArray body;
        QByteArray contentType = "application/json";
        int status = 200;
        if (header.startsWith("GET /health ")) {
            body = R"({"ok":true,"port":18380})";
        } else if (header.startsWith("GET /tool/screenshot ")) {
            ++screenshotRequests_;
            contentType = "application/x-xiaoyv-rgba";
            body = "XYVF";
            body.append(char(1)); body.append(char(0)); body.append(char(0)); body.append(char(0));
            body.append(char(1)); body.append(char(0)); body.append(char(0)); body.append(char(0));
            body.append(QByteArray::fromHex("010203FF"));
        } else if (header.startsWith("PUT /tool/image ")) {
            ++projectionUploads_;
            body = R"({"fileId":"image-1"})";
        } else if (header.startsWith("POST /jsonrpc ")) {
            const QJsonObject call = QJsonDocument::fromJson(requestBody).object();
            const int id = call.value(QStringLiteral("id")).toInt();
            const QString method = call.value(QStringLiteral("method")).toString();
            if (method == QLatin1String("script.run")) {
                pendingScript_ = socket;
                pendingScriptId_ = id;
                return;
            }
            if (method == QLatin1String("viewer.openImage")) ++projectionOpens_;
            QJsonValue result = true;
            if (method == QLatin1String("log.drain")) {
                result = QJsonObject{{QStringLiteral("entries"), QJsonArray{}},
                                     {QStringLiteral("lastId"), 0}};
            }
            body = QJsonDocument(QJsonObject{
                    {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                    {QStringLiteral("id"), id},
                    {QStringLiteral("result"), result},
            }).toJson(QJsonDocument::Compact);
            if (method == QLatin1String("script.stop") && pendingScript_ != nullptr) {
                QPointer<QTcpSocket> running = pendingScript_;
                const int runningId = pendingScriptId_;
                pendingScript_.clear();
                pendingScriptId_ = 0;
                QTimer::singleShot(150, this, [running, runningId] {
                    if (running == nullptr) return;
                    const QByteArray completed = QJsonDocument(QJsonObject{
                            {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                            {QStringLiteral("id"), runningId},
                            {QStringLiteral("result"), QJsonObject{{QStringLiteral("stopped"), true}}},
                    }).toJson(QJsonDocument::Compact);
                    writeResponse(running, completed);
                });
            }
        } else {
            status = 404;
            body = R"({"error":"not found"})";
        }
        writeResponse(socket, body, contentType, status);
    }

    QHash<QTcpSocket*, QByteArray> buffers_;
    // server_ 必须先于 buffers_ 析构；关闭服务器销毁 socket 时，destroyed 回调仍会访问 buffers_。
    QTcpServer server_;
    QPointer<QTcpSocket> pendingScript_;
    int pendingScriptId_ = 0;
    int screenshotRequests_ = 0;
    int projectionUploads_ = 0;
    int projectionOpens_ = 0;
};

class DeviceClientTest final : public QObject {
    Q_OBJECT

private slots:
    void allowsOmittedTransportCallbacks();
    void treatsHttpErrorAsReachableService();
    void connectsAndReadsRawFrame();
    void projectsImageWithFixedTwoStepProtocol();
    void keepsStopBusyUntilScriptActuallyEnds();
};

void DeviceClientTest::allowsOmittedTransportCallbacks() {
    DeviceTransport transport;
    DeviceConnectionSettings settings;
    settings.mode = DeviceConnectionSettings::Mode::Lan;
    settings.host.clear();
    settings.port = 0;
    transport.setSettings(settings);

    // 无效端点会同步进入失败路径；省略回调时也必须保持公开接口可直接调用。
    transport.get(QStringLiteral("/health"), 1, {}, {});
}

void DeviceClientTest::treatsHttpErrorAsReachableService() {
    MockDeviceServer server;
    QVERIFY(server.listen());
    DeviceTransport transport;
    DeviceConnectionSettings settings;
    settings.mode = DeviceConnectionSettings::Mode::Lan;
    settings.host = QStringLiteral("127.0.0.1");
    settings.port = server.port();
    transport.setSettings(settings);

    QSignalSpy available(&transport, &DeviceTransport::transportAvailable);
    QSignalSpy unavailable(&transport, &DeviceTransport::transportUnavailable);
    bool requestFailed = false;
    transport.get(
            QStringLiteral("/missing"),
            3000,
            {},
            [&requestFailed](const QString&) { requestFailed = true; });

    QTRY_VERIFY_WITH_TIMEOUT(requestFailed, 3000);
    QCOMPARE(available.size(), 1);
    QCOMPARE(unavailable.size(), 0);
}

void DeviceClientTest::connectsAndReadsRawFrame() {
    MockDeviceServer server;
    QVERIFY(server.listen());
    DeviceClient client;
    DeviceConnectionSettings settings;
    settings.mode = DeviceConnectionSettings::Mode::Lan;
    settings.host = QStringLiteral("127.0.0.1");
    settings.port = server.port();
    client.setSettings(settings);

    client.checkConnection();
    QTRY_COMPARE_WITH_TIMEOUT(
            client.connectionState(), DeviceClient::ConnectionState::Connected, 3000);

    QSignalSpy screenshot(&client, &DeviceClient::screenshotReceived);
    client.requestScreenshot();
    client.requestScreenshot();
    QTRY_COMPARE_WITH_TIMEOUT(screenshot.size(), 1, 3000);
    QCOMPARE(server.screenshotRequests(), 1);
    const QImage image = qvariant_cast<QImage>(screenshot.front().at(0));
    QCOMPARE(image.size(), QSize(1, 1));
    QCOMPARE(image.pixelColor(0, 0), QColor(1, 2, 3, 255));
}

void DeviceClientTest::projectsImageWithFixedTwoStepProtocol() {
    MockDeviceServer server;
    QVERIFY(server.listen());
    DeviceClient client;
    DeviceConnectionSettings settings;
    settings.mode = DeviceConnectionSettings::Mode::Lan;
    settings.host = QStringLiteral("127.0.0.1");
    settings.port = server.port();
    client.setSettings(settings);

    QImage image(2, 2, QImage::Format_RGBA8888);
    image.fill(Qt::green);
    QSignalSpy opened(&client, &DeviceClient::projectionOpened);
    client.projectImage(image);
    client.projectImage(image);
    QTRY_COMPARE_WITH_TIMEOUT(opened.size(), 1, 3000);
    QCOMPARE(server.projectionUploads(), 1);
    QCOMPARE(server.projectionOpens(), 1);
    QVERIFY(!client.isBusy(DeviceClient::Operation::Projection));
}

void DeviceClientTest::keepsStopBusyUntilScriptActuallyEnds() {
    MockDeviceServer server;
    QVERIFY(server.listen());
    DeviceClient client;
    DeviceConnectionSettings settings;
    settings.mode = DeviceConnectionSettings::Mode::Lan;
    settings.host = QStringLiteral("127.0.0.1");
    settings.port = server.port();
    client.setSettings(settings);

    QSignalSpy started(&client, &DeviceClient::scriptStarted);
    QSignalSpy finished(&client, &DeviceClient::scriptFinished);
    client.runScript(QStringLiteral("lua"), QStringLiteral("while true do end"));
    QTRY_COMPARE_WITH_TIMEOUT(started.size(), 1, 3000);
    QVERIFY(client.isScriptRunning());

    client.stopScript();
    QVERIFY(client.isBusy(DeviceClient::Operation::ScriptStop));
    QTest::qWait(50);
    QVERIFY(client.isBusy(DeviceClient::Operation::ScriptStop));
    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 3000);
    QVERIFY(!client.isScriptRunning());
    QVERIFY(!client.isBusy(DeviceClient::Operation::ScriptStop));
    QVERIFY(!client.isBusy(DeviceClient::Operation::Script));
}

QTEST_MAIN(DeviceClientTest)
#include "device_client_test.moc"
