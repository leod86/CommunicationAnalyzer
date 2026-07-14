#include <QApplication>

#include "ElaApplication.h"
#include "app/MainWindow.h"

/*****************************************************
函数名称：int main(int argc, char *argv[])
入口参数：argc为参数数量，argv为参数列表
出口参数：应用程序退出码
函数功能：设置全局界面缩放并启动通讯分析程序
*****************************************************/
int main(int argc, char* argv[])
{
    QApplication application(argc, argv);

    // 初始化Ela全局应用对象。
    eApp->init();

    // 创建并显示通讯分析主窗口。
    MainWindow mainWindow;
    mainWindow.show();

    return application.exec();
}
