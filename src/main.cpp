#include <QApplication>
#include <QCoreApplication>
#include <QTimer>

#include "ElaApplication.h"
#include "app/MainWindow.h"
#include "update/UpdateManager.h"

/*****************************************************
函数名称：int main(int argc, char *argv[])
入口参数：argc为参数数量，argv为参数列表
出口参数：应用程序退出码
函数功能：设置全局界面缩放并启动通讯分析程序
*****************************************************/
int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("通讯协议分析仪"));
    QCoreApplication::setOrganizationName(QStringLiteral("CommunicationAnalyzer"));
    QCoreApplication::setApplicationVersion(QStringLiteral(COMMUNICATION_ANALYZER_VERSION));

    // 初始化Ela全局应用对象。
    eApp->init();

    // 创建并显示通讯分析主窗口。
    MainWindow mainWindow;
    mainWindow.show();

    // 主窗口显示后检查更新，避免网络请求阻塞程序首帧。
    auto* updateManager = new UpdateManager(&mainWindow, &application);
    QTimer::singleShot(0, updateManager, [updateManager]() {
        updateManager->checkForUpdates();
    });

    return application.exec();
}
