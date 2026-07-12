#include "app/MainWindow.h"

#include "communication/SerialController.h"
#include "pages/MonitorPage.h"

/*****************************************************
函数名称：MainWindow::MainWindow(QWidget* parent)
入口参数：parent为父窗口
出口参数：无
函数功能：创建主窗口并初始化通讯监视功能
*****************************************************/
MainWindow::MainWindow(QWidget* parent)
    : ElaWindow(parent)
    , _monitorPage(new MonitorPage(this))
    , _serialController(new SerialController(this))
{
    // 初始化窗口及业务信号连接。
    initializeWindow();
    initializeConnections();

    // 首次启动时刷新可用串口。
    _serialController->refreshPorts();
}
/*****************************************************
函数名称：MainWindow::~MainWindow()
入口参数：无
出口参数：无
函数功能：释放通讯分析主窗口
*****************************************************/
MainWindow::~MainWindow() = default;

/*****************************************************
函数名称：void MainWindow::initializeWindow()
入口参数：无
出口参数：无
函数功能：配置窗口尺寸、标题和导航页面
*****************************************************/
void MainWindow::initializeWindow()
{
    // 配置主窗口基础属性。
    resize(1180, 760);
    setMinimumSize(900, 620);
    setWindowTitle(QStringLiteral("通讯分析器"));
    setUserInfoCardVisible(false);

    // 注册通讯监视导航页面。
    addPageNode(QStringLiteral("通讯监视"), _monitorPage, ElaIconType::Table);
}

/*****************************************************
函数名称：void MainWindow::initializeConnections()
入口参数：无
出口参数：无
函数功能：连接页面命令与串口控制器事件
*****************************************************/
void MainWindow::initializeConnections()
{
    // 将页面操作转发给串口控制器。
    connect(_monitorPage, &MonitorPage::refreshPortsRequested,
            _serialController, &SerialController::refreshPorts);
    connect(_monitorPage, &MonitorPage::openPortRequested,
            _serialController, &SerialController::openPort);
    connect(_monitorPage, &MonitorPage::closePortRequested,
            _serialController, &SerialController::closePort);
    connect(_monitorPage, &MonitorPage::sendDataRequested,
            _serialController, &SerialController::writeData);

    // 将串口状态和数据反馈到页面。
    connect(_serialController, &SerialController::portsUpdated,
            _monitorPage, &MonitorPage::updatePorts);
    connect(_serialController, &SerialController::connectionStateChanged,
            _monitorPage, &MonitorPage::setConnectionState);
    connect(_serialController, &SerialController::dataReceived,
            _monitorPage, &MonitorPage::appendReceivedData);
    connect(_serialController, &SerialController::dataSent,
            _monitorPage, &MonitorPage::appendSentData);
    connect(_serialController, &SerialController::errorOccurred,
            _monitorPage, &MonitorPage::showError);
}
