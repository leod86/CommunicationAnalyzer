#include "communication/SerialController.h"

#include <QSerialPortInfo>

#include <algorithm>

namespace
{
// 连续数据流的最长聚合时间，保证界面周期性刷新。
constexpr int kMaximumFrameAccumulationMilliseconds = 500;
// 单个显示批次的最大缓存字节数，避免持续流耗尽内存。
constexpr qsizetype kMaximumFrameBytes = 64 * 1024;
}

/*****************************************************
函数名称：SerialController::SerialController(QObject* parent)
入口参数：parent为父对象
出口参数：无
函数功能：初始化串口控制器并连接串口事件
*****************************************************/
SerialController::SerialController(QObject* parent)
    : QObject(parent)
{
    // 连接串口接收与错误事件。
    connect(&_serialPort, &QSerialPort::readyRead, this, &SerialController::handleReadyRead);
    connect(&_serialPort, &QSerialPort::errorOccurred, this, &SerialController::handleSerialError);
    connect(&_portMonitorTimer, &QTimer::timeout, this, [this]() {
        updateAvailablePorts(false);
    });
    _portMonitorTimer.setInterval(1000);
    _portMonitorTimer.start();

    _frameTimer.setSingleShot(true);
    connect(&_frameTimer, &QTimer::timeout, this, &SerialController::flushReceivedFrame);
}

/*****************************************************
函数名称：SerialController::~SerialController()
入口参数：无
出口参数：无
函数功能：释放控制器前关闭串口
*****************************************************/
SerialController::~SerialController()
{
    if (_serialPort.isOpen())
    {
        _serialPort.close();
    }
}

/*****************************************************
函数名称：void SerialController::refreshPorts()
入口参数：无
出口参数：无
函数功能：获取并发布当前可用串口及其描述
*****************************************************/
void SerialController::refreshPorts()
{
    updateAvailablePorts(true);
}

/*****************************************************
函数名称：void SerialController::updateAvailablePorts(bool forceUpdate)
入口参数：forceUpdate为是否强制通知界面
出口参数：无
函数功能：扫描串口列表，并在设备插拔或描述变化时刷新界面
*****************************************************/
void SerialController::updateAvailablePorts(bool forceUpdate)
{
    QList<QSerialPortInfo> ports = QSerialPortInfo::availablePorts();
    std::sort(ports.begin(), ports.end(), [](const QSerialPortInfo& left, const QSerialPortInfo& right) {
        return left.portName().localeAwareCompare(right.portName()) < 0;
    });

    QStringList portNames;
    QStringList descriptions;
    for (const QSerialPortInfo& port : ports)
    {
        portNames.append(port.portName());
        descriptions.append(port.description());
    }

    if (!forceUpdate && portNames == _lastPortNames && descriptions == _lastPortDescriptions)
    {
        return;
    }

    _lastPortNames = portNames;
    _lastPortDescriptions = descriptions;
    emit portsUpdated(_lastPortNames, _lastPortDescriptions);
}

/*****************************************************
函数名称：void SerialController::openPort(const QString& portName, qint32 baudRate, int dataBits, int parity, int stopBits)
入口参数：portName为串口名，baudRate为波特率，dataBits为数据位，parity为校验位，stopBits为停止位
出口参数：无
函数功能：使用完整串口参数打开指定设备
*****************************************************/
void SerialController::openPort(const QString& portName, qint32 baudRate, int dataBits, int parity, int stopBits)
{
    if (portName.isEmpty())
    {
        emit errorOccurred(QStringLiteral("请选择有效串口"));
        return;
    }

    if (_serialPort.isOpen())
    {
        _serialPort.close();
    }
    _frameTimer.stop();
    _frameStartTimer.invalidate();
    _receiveBuffer.clear();

    _serialPort.setPortName(portName);
    const bool configured = _serialPort.setBaudRate(baudRate)
        && _serialPort.setDataBits(static_cast<QSerialPort::DataBits>(dataBits))
        && _serialPort.setParity(static_cast<QSerialPort::Parity>(parity))
        && _serialPort.setStopBits(static_cast<QSerialPort::StopBits>(stopBits))
        && _serialPort.setFlowControl(QSerialPort::NoFlowControl);

    if (!configured)
    {
        emit errorOccurred(QStringLiteral("串口参数不受当前设备支持"));
        emit connectionStateChanged(false, QStringLiteral("未连接"));
        return;
    }

    if (!_serialPort.open(QIODevice::ReadWrite))
    {
        emit errorOccurred(QStringLiteral("打开 %1 失败：%2").arg(portName, _serialPort.errorString()));
        emit connectionStateChanged(false, QStringLiteral("未连接"));
        return;
    }

    emit connectionStateChanged(true, QStringLiteral("%1 @ %2").arg(portName).arg(baudRate));
}

/*****************************************************
函数名称：void SerialController::closePort()
入口参数：无
出口参数：无
函数功能：关闭当前串口并发布连接状态
*****************************************************/
void SerialController::closePort()
{
    flushReceivedFrame();
    if (_serialPort.isOpen())
    {
        _serialPort.close();
    }

    emit connectionStateChanged(false, QStringLiteral("未连接"));
}

/*****************************************************
函数名称：void SerialController::writeData(const QByteArray& data)
入口参数：data为待发送字节数据
出口参数：无
函数功能：向已打开串口异步写入数据
*****************************************************/
void SerialController::writeData(const QByteArray& data)
{
    if (!_serialPort.isOpen())
    {
        emit errorOccurred(QStringLiteral("串口未连接"));
        return;
    }

    if (data.isEmpty())
    {
        emit errorOccurred(QStringLiteral("发送数据不能为空"));
        return;
    }

    if (_serialPort.write(data) < 0)
    {
        emit errorOccurred(QStringLiteral("发送失败：%1").arg(_serialPort.errorString()));
        return;
    }

    emit dataSent(data);
}

/*****************************************************
函数名称：void SerialController::setFrameTimeout(int milliseconds)
入口参数：milliseconds为断帧等待时间，单位毫秒
出口参数：无
函数功能：更新串口接收数据的静默断帧时间
*****************************************************/
void SerialController::setFrameTimeout(int milliseconds)
{
    _frameTimeoutMilliseconds = qBound(1, milliseconds, 5000);
    if (_frameTimer.isActive())
    {
        _frameTimer.start(_frameTimeoutMilliseconds);
    }
}

/*****************************************************
函数名称：void SerialController::handleReadyRead()
入口参数：无
出口参数：无
函数功能：读取并发布串口当前接收数据
*****************************************************/
void SerialController::handleReadyRead()
{
    const QByteArray receivedData = _serialPort.readAll();
    if (receivedData.isEmpty())
    {
        return;
    }

    if (_receiveBuffer.isEmpty())
    {
        _frameStartTimer.start();
    }
    _receiveBuffer.append(receivedData);

    if (_receiveBuffer.size() >= kMaximumFrameBytes
        || _frameStartTimer.elapsed() >= kMaximumFrameAccumulationMilliseconds)
    {
        flushReceivedFrame();
        return;
    }

    _frameTimer.start(_frameTimeoutMilliseconds);
}

/*****************************************************
函数名称：void SerialController::flushReceivedFrame()
入口参数：无
出口参数：无
函数功能：静默时间到达后发布并清空当前完整接收帧
*****************************************************/
void SerialController::flushReceivedFrame()
{
    _frameTimer.stop();
    if (_receiveBuffer.isEmpty())
    {
        _frameStartTimer.invalidate();
        return;
    }

    const QByteArray completeFrame = _receiveBuffer;
    _receiveBuffer.clear();
    _frameStartTimer.invalidate();
    emit dataReceived(completeFrame);
}

/*****************************************************
函数名称：void SerialController::handleSerialError(QSerialPort::SerialPortError error)
入口参数：error为Qt串口错误码
出口参数：无
函数功能：发布串口错误并处理设备断开状态
*****************************************************/
void SerialController::handleSerialError(QSerialPort::SerialPortError error)
{
    if (error == QSerialPort::NoError)
    {
        return;
    }

    emit errorOccurred(_serialPort.errorString());
    if (error == QSerialPort::ResourceError)
    {
        closePort();
    }
}
