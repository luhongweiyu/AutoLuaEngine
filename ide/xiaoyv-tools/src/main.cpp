/**
 * 文件用途：配置 Qt 应用标识、主题和主窗口启动参数。
 */
#include "main_window.h"

#include "ui/app_theme.h"

#include <QApplication>
#include <QFont>

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    // 使用完整字体 hinting 和无抗锯齿策略，让文字边缘优先贴合像素网格。
    // 这是抓图取色工具的桌面工作界面，允许少量锯齿来换取坐标、表格和按钮文字的清晰轮廓。
    QFont applicationFont = application.font();
    applicationFont.setFamily(QStringLiteral("宋体"));
    applicationFont.setPointSize(9);
    applicationFont.setHintingPreference(QFont::PreferFullHinting);
    applicationFont.setStyleStrategy(QFont::NoAntialias);
    application.setFont(applicationFont);
    QCoreApplication::setOrganizationName(QStringLiteral("xiaoyv"));
    QCoreApplication::setApplicationName(QStringLiteral("xiaoyv-tools"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0.0"));
    xiaoyv::tools::applyApplicationTheme(&application, xiaoyv::tools::loadTheme());

    xiaoyv::tools::MainWindow window(application.arguments());
    window.show();
    return application.exec();
}
