#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "ElaWindow.h"

class MonitorPage;
class SerialController;
class SettingPage;

class MainWindow final : public ElaWindow
{
    Q_OBJECT

public:
    // 创建通讯分析主窗口。
    explicit MainWindow(QWidget* parent = nullptr);
    // 释放通讯分析主窗口。
    ~MainWindow() override;

private:
    // 初始化主窗口外观与导航页面。
    void initializeWindow();
    // 建立页面和串口控制器之间的信号连接。
    void initializeConnections();

    MonitorPage* _monitorPage{nullptr};
    SettingPage* _settingPage{nullptr};
    SerialController* _serialController{nullptr};
    QString _monitorPageKey;
    QString _settingPageKey;
};

#endif // MAINWINDOW_H
