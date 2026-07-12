#ifndef SERIALCONTROLLER_H
#define SERIALCONTROLLER_H

#include <QObject>
#include <QSerialPort>
#include <QStringList>

class SerialController final : public QObject
{
    Q_OBJECT

public:
    // 创建串口控制器。
    explicit SerialController(QObject* parent = nullptr);
    // 释放串口控制器并关闭串口。
    ~SerialController() override;

public slots:
    // 刷新当前可用串口列表。
    void refreshPorts();
    // 使用指定端口和波特率打开串口。
    void openPort(const QString& portName, qint32 baudRate);
    // 关闭当前串口。
    void closePort();
    // 向当前串口写入数据。
    void writeData(const QByteArray& data);

signals:
    // 通知界面更新可用串口列表。
    void portsUpdated(const QStringList& portNames);
    // 通知界面更新连接状态。
    void connectionStateChanged(bool connected, const QString& description);
    // 通知界面显示接收数据。
    void dataReceived(const QByteArray& data);
    // 通知界面显示已发送数据。
    void dataSent(const QByteArray& data);
    // 通知界面显示串口错误。
    void errorOccurred(const QString& message);

private slots:
    // 读取串口接收缓冲区。
    void handleReadyRead();
    // 处理Qt串口错误事件。
    void handleSerialError(QSerialPort::SerialPortError error);

private:
    QSerialPort _serialPort;
};

#endif // SERIALCONTROLLER_H
