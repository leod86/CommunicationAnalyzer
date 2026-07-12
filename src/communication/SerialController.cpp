#include "communication/SerialController.h"

#include <QSerialPortInfo>

#include <algorithm>

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
}
/*****************************************************
函数名称：SerialController::~SerialController()
入口参数：无
出口参数：无
函数功能：释放控制器前关闭串口
*****************************************************/
SerialController::~SerialController()
{
    // 关闭可能仍处于打开状态的串口。
    closePort();
}

/*****************************************************
函数名称：void SerialController::refreshPorts()
入口参数：无
出口参数：无
函数功能：获取并发布当前可用串口列表
*****************************************************/
void SerialController::refreshPorts()
{
    QStringList portNames;

    // 收集系统当前可枚举的串口名称。
    const QList<QSerialPortInfo> ports = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo& port : ports)
    {
        portNames.append(port.portName());
    }

    // 保持串口列表显示顺序稳定。
    std::sort(portNames.begin(), portNames.end());
    emit portsUpdated(portNames);
}

/*****************************************************
函数名称：void SerialController::openPort(const QString& portName, qint32 baudRate)
入口参数：portName为串口名称，baudRate为波特率
出口参数：无
函数功能：按照8N1无流控参数打开指定串口
*****************************************************/
void SerialController::openPort(const QString& portName, qint32 baudRate)
{
    if (portName.isEmpty())
    {
        emit errorOccurred(QStringLiteral("请选择有效串口"));
        return;
    }

    // 切换端口前先关闭当前连接。
    if (_serialPort.isOpen())
    {
        _serialPort.close();
    }

    // 配置通用8N1串口参数。
    _serialPort.setPortName(portName);
    _serialPort.setBaudRate(baudRate);
    _serialPort.setDataBits(QSerialPort::Data8);
    _serialPort.setParity(QSerialPort::NoParity);
    _serialPort.setStopBits(QSerialPort::OneStop);
    _serialPort.setFlowControl(QSerialPort::NoFlowControl);

    // 以异步读写模式打开串口。
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
    // 仅在串口已打开时执行关闭操作。
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

    // 写入Qt串口发送缓冲区。
    if (_serialPort.write(data) < 0)
    {
        emit errorOccurred(QStringLiteral("发送失败：%1").arg(_serialPort.errorString()));
        return;
    }

    emit dataSent(data);
}

/*****************************************************
函数名称：void SerialController::handleReadyRead()
入口参数：无
出口参数：无
函数功能：读取并发布串口当前接收数据
*****************************************************/
void SerialController::handleReadyRead()
{
    // 一次读取当前缓冲区全部数据，避免残留阻塞后续解析。
    const QByteArray data = _serialPort.readAll();
    if (!data.isEmpty())
    {
        emit dataReceived(data);
    }
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

    // 设备被移除或资源不可用时终止当前连接。
    if (error == QSerialPort::ResourceError)
    {
        closePort();
    }
}
