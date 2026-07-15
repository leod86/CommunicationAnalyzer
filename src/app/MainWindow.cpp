#include "app/MainWindow.h"

#include "communication/SerialController.h"
#include "ElaToolBar.h"
#include "pages/MonitorPage.h"
#include "pages/SettingPage.h"

/*****************************************************
函数名称：MainWindow::MainWindow(QWidget* parent)
入口参数：parent为父窗口
出口参数：无
函数功能：创建主窗口并初始化通讯监视功能
*****************************************************/
MainWindow::MainWindow(QWidget* parent)
    : ElaWindow(parent)
    , _monitorPage(new MonitorPage(this))
    , _settingPage(new SettingPage(this))
    , _serialController(new SerialController(this))
{
    // 初始化窗口及业务信号连接。
    initializeWindow();
    initializeConnections();

    // 将已保存的断帧时间同步到串口控制器。
    _serialController->setFrameTimeout(_monitorPage->frameTimeout());

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
    resize(1000, 650);
    setMinimumSize(520, 360);
    setWindowTitle(QStringLiteral("通讯协议分析仪"));
    setUserInfoCardVisible(false);
    setNavigationBarDisplayMode(ElaNavigationType::Auto);

    // 将串口配置固定在窗口顶部，避免占用通讯页面的垂直空间。
    addToolBar(Qt::TopToolBarArea, _monitorPage->serialToolBar());

    // 注册通讯监视页面和底部设置页面。
    addPageNode(QStringLiteral("串口监视"), _monitorPage, ElaIconType::Table);
    _monitorPageKey = _monitorPage->property("ElaPageKey").toString();
    addFooterNode(QStringLiteral("设置"), _settingPage, _settingPageKey, 0, ElaIconType::Gear);
    _settingPage->applySavedSettings();
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
    connect(_monitorPage, &MonitorPage::frameTimeoutChanged,
            _serialController, &SerialController::setFrameTimeout);

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

    // 仅在串口监视页面显示串口配置工具栏。
    connect(this, &ElaWindow::navigationNodeClicked, this,
            [this](ElaNavigationType::NavigationNodeType, const QString& nodeKey) {
        _monitorPage->serialToolBar()->setVisible(nodeKey == _monitorPageKey);
    });
}
