#include "pages/MonitorPage.h"

#include "ElaCheckBox.h"
#include "ElaComboBox.h"
#include "ElaLineEdit.h"
#include "ElaPlainTextEdit.h"
#include "ElaPushButton.h"
#include "ElaText.h"

#include <QDateTime>
#include <QHBoxLayout>
#include <QRegularExpression>
#include <QVBoxLayout>

/*****************************************************
函数名称：MonitorPage::MonitorPage(QWidget* parent)
入口参数：parent为父窗口
出口参数：无
函数功能：初始化通讯监视页面
*****************************************************/
MonitorPage::MonitorPage(QWidget* parent)
    : QWidget(parent)
{
    // 创建页面控件并连接交互事件。
    buildUi();
}
/*****************************************************
函数名称：MonitorPage::~MonitorPage()
入口参数：无
出口参数：无
函数功能：释放通讯监视页面
*****************************************************/
MonitorPage::~MonitorPage() = default;

/*****************************************************
函数名称：void MonitorPage::buildUi()
入口参数：无
出口参数：无
函数功能：创建通讯监视页面控件和布局
*****************************************************/
void MonitorPage::buildUi()
{
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(24, 20, 24, 24);
    rootLayout->setSpacing(12);

    auto* connectionLayout = new QHBoxLayout();
    connectionLayout->setSpacing(10);

    auto* portLabel = new ElaText(QStringLiteral("串口"), this);
    _portComboBox = new ElaComboBox(this);
    _portComboBox->setMinimumWidth(140);

    auto* baudLabel = new ElaText(QStringLiteral("波特率"), this);
    _baudComboBox = new ElaComboBox(this);
    _baudComboBox->addItems({QStringLiteral("9600"), QStringLiteral("19200"), QStringLiteral("38400"),
                             QStringLiteral("57600"), QStringLiteral("115200"), QStringLiteral("230400"),
                             QStringLiteral("460800"), QStringLiteral("921600")});
    _baudComboBox->setCurrentText(QStringLiteral("115200"));
    _baudComboBox->setMinimumWidth(120);

    _refreshButton = new ElaPushButton(QStringLiteral("刷新"), this);
    _openCloseButton = new ElaPushButton(QStringLiteral("打开"), this);
    _openCloseButton->setEnabled(false);

    // 构建紧凑的串口连接工具栏。
    connectionLayout->addWidget(portLabel);
    connectionLayout->addWidget(_portComboBox);
    connectionLayout->addWidget(baudLabel);
    connectionLayout->addWidget(_baudComboBox);
    connectionLayout->addWidget(_refreshButton);
    connectionLayout->addWidget(_openCloseButton);
    connectionLayout->addStretch();

    _statusText = new ElaText(QStringLiteral("未连接"), this);
    _statusText->setMinimumHeight(24);

    auto* receiveTitle = new ElaText(QStringLiteral("通讯记录"), 16, this);
    _trafficView = new ElaPlainTextEdit(this);
    _trafficView->setReadOnly(true);
    _trafficView->setMinimumHeight(380);
    _trafficView->setPlaceholderText(QStringLiteral("串口收发数据将在这里显示"));

    auto* transmitTitle = new ElaText(QStringLiteral("发送数据"), 16, this);
    auto* transmitLayout = new QHBoxLayout();
    transmitLayout->setSpacing(10);

    _transmitEdit = new ElaLineEdit(this);
    _transmitEdit->setPlaceholderText(QStringLiteral("输入文本，HEX模式示例：01 03 00 00 00 02"));
    _hexModeCheckBox = new ElaCheckBox(QStringLiteral("HEX模式"), this);
    _hexModeCheckBox->setChecked(true);
    _sendButton = new ElaPushButton(QStringLiteral("发送"), this);
    _sendButton->setEnabled(false);
    _clearButton = new ElaPushButton(QStringLiteral("清空"), this);

    // 构建发送区域。
    transmitLayout->addWidget(_transmitEdit, 1);
    transmitLayout->addWidget(_hexModeCheckBox);
    transmitLayout->addWidget(_sendButton);
    transmitLayout->addWidget(_clearButton);

    // 将各功能区加入页面主布局。
    rootLayout->addLayout(connectionLayout);
    rootLayout->addWidget(_statusText);
    rootLayout->addWidget(receiveTitle);
    rootLayout->addWidget(_trafficView, 1);
    rootLayout->addWidget(transmitTitle);
    rootLayout->addLayout(transmitLayout);

    // 连接页面按钮与业务槽函数。
    connect(_refreshButton, &ElaPushButton::clicked, this, &MonitorPage::refreshPortsRequested);
    connect(_openCloseButton, &ElaPushButton::clicked, this, &MonitorPage::handleOpenCloseClicked);
    connect(_sendButton, &ElaPushButton::clicked, this, &MonitorPage::handleSendClicked);
    connect(_clearButton, &ElaPushButton::clicked, this, &MonitorPage::clearTraffic);
    connect(_transmitEdit, &ElaLineEdit::returnPressed, this, &MonitorPage::handleSendClicked);
}

/*****************************************************
函数名称：void MonitorPage::updatePorts(const QStringList& portNames)
入口参数：portNames为可用串口名称列表
出口参数：无
函数功能：刷新串口选择框并保持原选择
*****************************************************/
void MonitorPage::updatePorts(const QStringList& portNames)
{
    const QString currentPort = _portComboBox->currentText();

    // 更新串口选择列表。
    _portComboBox->clear();
    _portComboBox->addItems(portNames);

    const int previousIndex = _portComboBox->findText(currentPort);
    if (previousIndex >= 0)
    {
        _portComboBox->setCurrentIndex(previousIndex);
    }

    _openCloseButton->setEnabled(_connected || !portNames.isEmpty());
    if (!_connected && portNames.isEmpty())
    {
        _statusText->setText(QStringLiteral("未发现可用串口"));
    }
}

/*****************************************************
函数名称：void MonitorPage::setConnectionState(bool connected, const QString& description)
入口参数：connected为连接状态，description为状态描述
出口参数：无
函数功能：根据串口连接状态更新页面控件
*****************************************************/
void MonitorPage::setConnectionState(bool connected, const QString& description)
{
    _connected = connected;

    // 连接后锁定串口参数，断开后恢复编辑。
    _portComboBox->setEnabled(!connected);
    _baudComboBox->setEnabled(!connected);
    _refreshButton->setEnabled(!connected);
    _openCloseButton->setText(connected ? QStringLiteral("关闭") : QStringLiteral("打开"));
    _openCloseButton->setEnabled(connected || _portComboBox->count() > 0);
    _sendButton->setEnabled(connected);
    _statusText->setText(connected ? QStringLiteral("已连接：%1").arg(description) : description);
}

/*****************************************************
函数名称：void MonitorPage::appendReceivedData(const QByteArray& data)
入口参数：data为串口接收数据
出口参数：无
函数功能：追加一条接收记录
*****************************************************/
void MonitorPage::appendReceivedData(const QByteArray& data)
{
    // 追加RX方向数据。
    appendTraffic(QStringLiteral("RX"), data);
}

/*****************************************************
函数名称：void MonitorPage::appendSentData(const QByteArray& data)
入口参数：data为串口发送数据
出口参数：无
函数功能：追加一条发送记录
*****************************************************/
void MonitorPage::appendSentData(const QByteArray& data)
{
    // 追加TX方向数据。
    appendTraffic(QStringLiteral("TX"), data);
}

/*****************************************************
函数名称：void MonitorPage::showError(const QString& message)
入口参数：message为错误描述
出口参数：无
函数功能：在状态区域显示错误信息
*****************************************************/
void MonitorPage::showError(const QString& message)
{
    _statusText->setText(QStringLiteral("错误：%1").arg(message));
}

/*****************************************************
函数名称：void MonitorPage::handleOpenCloseClicked()
入口参数：无
出口参数：无
函数功能：根据当前状态请求打开或关闭串口
*****************************************************/
void MonitorPage::handleOpenCloseClicked()
{
    if (_connected)
    {
        emit closePortRequested();
        return;
    }

    // 使用界面当前参数请求打开串口。
    emit openPortRequested(_portComboBox->currentText(), _baudComboBox->currentText().toInt());
}

/*****************************************************
函数名称：void MonitorPage::handleSendClicked()
入口参数：无
出口参数：无
函数功能：解析输入内容并请求发送串口数据
*****************************************************/
void MonitorPage::handleSendClicked()
{
    bool valid = false;

    // 按当前显示模式解析发送数据。
    const QByteArray data = parseTransmitData(valid);
    if (!valid)
    {
        showError(QStringLiteral("HEX数据必须由偶数个十六进制字符组成"));
        return;
    }

    if (data.isEmpty())
    {
        showError(QStringLiteral("发送数据不能为空"));
        return;
    }

    emit sendDataRequested(data);
}

/*****************************************************
函数名称：void MonitorPage::clearTraffic()
入口参数：无
出口参数：无
函数功能：清空当前通讯记录
*****************************************************/
void MonitorPage::clearTraffic()
{
    _trafficView->clear();
}

/*****************************************************
函数名称：QByteArray MonitorPage::parseTransmitData(bool& valid) const
入口参数：valid返回输入内容是否合法
出口参数：解析后的字节数据
函数功能：按照文本或HEX模式解析发送内容
*****************************************************/
QByteArray MonitorPage::parseTransmitData(bool& valid) const
{
    const QString input = _transmitEdit->text();
    if (!_hexModeCheckBox->isChecked())
    {
        valid = true;
        return input.toUtf8();
    }

    QString normalized = input;
    normalized.remove(QRegularExpression(QStringLiteral("\\s+")));

    const QRegularExpression hexPattern(QStringLiteral("^[0-9A-Fa-f]*$"));
    valid = (normalized.size() % 2 == 0) && hexPattern.match(normalized).hasMatch();
    return valid ? QByteArray::fromHex(normalized.toLatin1()) : QByteArray();
}

/*****************************************************
函数名称：void MonitorPage::appendTraffic(const QString& direction, const QByteArray& data)
入口参数：direction为数据方向，data为字节数据
出口参数：无
函数功能：格式化并追加带时间戳的通讯记录
*****************************************************/
void MonitorPage::appendTraffic(const QString& direction, const QByteArray& data)
{
    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"));
    const QString content = _hexModeCheckBox->isChecked()
        ? QString::fromLatin1(data.toHex(' ').toUpper())
        : QString::fromUtf8(data);

    // 使用统一格式记录收发方向、时间和数据内容。
    _trafficView->appendPlainText(QStringLiteral("[%1] %2  %3").arg(timestamp, direction, content));
}
