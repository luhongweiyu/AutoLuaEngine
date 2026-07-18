/**
 * 文件用途：验证主窗口、主题、动作目录和连接对话框能够完整构造。
 */
#include <QtTest/QTest>

#include "dialogs/connection_dialog.h"
#include "main_window.h"
#include "model/image_document.h"
#include "ui/action_catalog.h"
#include "ui/app_theme.h"
#include "workspace/image_workspace.h"

#include <QAction>
#include <QApplication>
#include <QDockWidget>
#include <QLabel>
#include <QRegularExpression>
#include <QTabBar>
#include <QTabWidget>
#include <QTableView>
#include <QToolButton>

using namespace xiaoyv::tools;

class AppSmokeTest final : public QObject {
    Q_OBJECT

private slots:
    void actionCatalogIsComplete();
    void toolIconsUseHardEdges();
    void themesAreRegistered();
    void mainWindowBuilds();
    void connectionDialogKeepsValues();
};

void AppSmokeTest::actionCatalogIsComplete() {
    QCOMPARE(kActionCatalog.size(), static_cast<std::size_t>(ActionId::Count));
    QSet<QString> names;
    for (const ActionSpec& spec : kActionCatalog) {
        const QString name = QString::fromUtf8(spec.objectName);
        QVERIFY2(!name.isEmpty(), spec.text);
        QVERIFY2(!names.contains(name), spec.objectName);
        names.insert(name);
    }
}

void AppSmokeTest::toolIconsUseHardEdges() {
    for (const ActionSpec& spec : kActionCatalog) {
        if (!spec.hasIcon) continue;
        for (QIcon::Mode mode : {QIcon::Normal, QIcon::Disabled}) {
            const QImage image = makeToolIcon(spec.icon, false)
                    .pixmap(QSize(22, 22), mode, QIcon::Off)
                    .toImage();
            QVERIFY(!image.isNull());
            for (int y = 0; y < image.height(); ++y) {
                for (int x = 0; x < image.width(); ++x) {
                    const int alpha = image.pixelColor(x, y).alpha();
                    QVERIFY2(alpha == 0 || alpha == 255, spec.text);
                }
            }
        }
    }
}

void AppSmokeTest::themesAreRegistered() {
    QVERIFY(allThemes().size() >= 4);
    for (AppTheme theme : allThemes()) QVERIFY(!themeDisplayName(theme).isEmpty());
}

void AppSmokeTest::mainWindowBuilds() {
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QVERIFY(window.findChild<QAction*>(QStringLiteral("openAction")) != nullptr);
    QVERIFY(window.findChild<QWidget*>(QStringLiteral("imageCanvas")) != nullptr);
    auto* workspace = window.findChild<ImageWorkspace*>();
    auto* colorTable = window.findChild<QTableView*>(QStringLiteral("colorPointTable"));
    QVERIFY(workspace != nullptr);
    QVERIFY(colorTable != nullptr);
    auto* inspectorTabs = window.findChild<QTabWidget*>(QStringLiteral("inspectorTabs"));
    QVERIFY(inspectorTabs != nullptr);
    QCOMPARE(inspectorTabs->count(), 3);
    QCOMPARE(inspectorTabs->tabText(0), QString::fromUtf8("取色"));
    QCOMPARE(inspectorTabs->tabText(1), QString::fromUtf8("二值化"));
    QCOMPARE(inspectorTabs->tabText(2), QString::fromUtf8("字库"));
    const auto* tabBar = inspectorTabs->tabBar();
    QCOMPARE(tabBar->tabRect(0).width(), tabBar->tabRect(1).width());
    QCOMPARE(tabBar->tabRect(1).width(), tabBar->tabRect(2).width());
    QVERIFY(tabBar->tabRect(0).width() <= 72);
    QImage image(8, 8, QImage::Format_RGBA8888);
    image.fill(Qt::white);
    QVERIFY(workspace->addImage(image, {}, QString::fromUtf8("集成测试")));
    QCOMPARE(colorTable->model(), workspace->currentDocument()->colorPoints());
    auto* imageStatus = window.findChild<QLabel*>(QStringLiteral("imageStatus"));
    auto* zoomStatus = window.findChild<QLabel*>(QStringLiteral("zoomStatus"));
    QVERIFY(imageStatus != nullptr);
    QVERIFY(zoomStatus != nullptr);
    QCOMPARE(imageStatus->text(), QStringLiteral("8 x 8"));
    QVERIFY(zoomStatus->x() < imageStatus->x());
    auto* imageDocuments = workspace->findChild<QTabWidget*>(QStringLiteral("imageDocuments"));
    QVERIFY(imageDocuments != nullptr);
    QVERIFY(imageDocuments->tabBar()->tabRect(0).height() <= 23);

    QVERIFY(workspace->addImage(image));
    QVERIFY(QRegularExpression(QStringLiteral("^\\d{6}_\\d{6}$"))
            .match(workspace->currentDocument()->displayName())
            .hasMatch());

    auto* inspectorDock = window.findChild<QDockWidget*>(QStringLiteral("inspectorDock"));
    QVERIFY(inspectorDock != nullptr);
    QVERIFY(inspectorDock->minimumWidth() <= 280);
    QVERIFY(inspectorDock->minimumSizeHint().width() <= 300);
    QCOMPARE(window.corner(Qt::BottomRightCorner), Qt::RightDockWidgetArea);
    inspectorDock->show();
    inspectorTabs->setCurrentIndex(0);
    QApplication::processEvents();
    const QList<QToolButton*> grips = window.findChildren<QToolButton*>(
            QStringLiteral("dockOperationGrip"));
    QVERIFY(grips.size() >= 3);
    for (QToolButton* grip : grips) {
        auto* dock = qobject_cast<QDockWidget*>(grip->parentWidget());
        QVERIFY(dock != nullptr);
        QVERIFY(grip->x() >= dock->width() - grip->width() - 4);
        auto* floatingTitle = dock->findChild<QWidget*>(QStringLiteral("floatingDockTitleBar"));
        QVERIFY(floatingTitle != nullptr);
        QVERIFY(!floatingTitle->isVisible());
    }
    ImageDocument* current = workspace->currentDocument();
    current->colorPoints()->addPoint(QPoint(0, 0), Qt::white);
    QTest::qWait(50);
    QVERIFY(!current->hasPreview());
    inspectorTabs->setCurrentIndex(1);
    QTRY_VERIFY_WITH_TIMEOUT(current->hasPreview(), 3000);
    inspectorTabs->setCurrentIndex(0);
    QTRY_VERIFY_WITH_TIMEOUT(!current->hasPreview(), 3000);
}

void AppSmokeTest::connectionDialogKeepsValues() {
    DeviceConnectionSettings input;
    input.mode = DeviceConnectionSettings::Mode::Adb;
    input.adbPath = QStringLiteral("D:/adb.exe");
    input.serial = QStringLiteral("emulator-5560");
    input.remotePort = 19000;
    ConnectionDialog dialog(input);
    const DeviceConnectionSettings output = dialog.value();
    QCOMPARE(output.mode, input.mode);
    QCOMPARE(output.adbPath, input.adbPath);
    QCOMPARE(output.serial, input.serial);
    QCOMPARE(output.remotePort, input.remotePort);
}

QTEST_MAIN(AppSmokeTest)
#include "app_smoke_test.moc"
