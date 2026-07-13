#ifndef SERIALCONTROLLER_H
#define SERIALCONTROLLER_H

#include <QObject>
#include <QSerialPort>
#include <QStringList>
#include <QTimer>

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
    // 使用界面选择的完整参数打开串口。
    void openPort(const QString& portName, qint32 baudRate, int dataBits, int parity, int stopBits);
    // 关闭当前串口。
    void closePort();
    // 向当前串口写入数据。
    void writeData(const QByteArray& data);
    // 设置接收数据的断帧等待时间。
    void setFrameTimeout(int milliseconds);

signals:
    // 通知界面更新串口名称和设备描述。
    void portsUpdated(const QStringList& portNames, const QStringList& descriptions);
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
    // 处理 Qt 串口错误事件。
    void handleSerialError(QSerialPort::SerialPortError error);
    // 断帧计时结束后发布完整接收帧。
    void flushReceivedFrame();

private:
    // 扫描串口并仅在列表变化或强制刷新时通知界面。
    void updateAvailablePorts(bool forceUpdate);

    QSerialPort _serialPort;
    QTimer _portMonitorTimer;
    QTimer _frameTimer;
    QByteArray _receiveBuffer;
    int _frameTimeoutMilliseconds{20};
    QStringList _lastPortNames;
    QStringList _lastPortDescriptions;
};

#endif // SERIALCONTROLLER_H
