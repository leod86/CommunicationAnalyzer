#ifndef MONITORPAGE_H
#define MONITORPAGE_H

#include <QByteArray>
#include <QStringList>
#include <QWidget>

class ElaCheckBox;
class ElaComboBox;
class ElaLineEdit;
class ElaPlainTextEdit;
class ElaPushButton;
class ElaText;

class MonitorPage final : public QWidget
{
    Q_OBJECT

public:
    // 创建通讯监视页面。
    explicit MonitorPage(QWidget* parent = nullptr);
    // 释放通讯监视页面。
    ~MonitorPage() override;

signals:
    // 请求刷新可用串口。
    void refreshPortsRequested();
    // 请求打开指定串口。
    void openPortRequested(const QString& portName, qint32 baudRate);
    // 请求关闭当前串口。
    void closePortRequested();
    // 请求发送字节数据。
    void sendDataRequested(const QByteArray& data);

public slots:
    // 更新页面中的串口列表。
    void updatePorts(const QStringList& portNames);
    // 更新串口连接状态。
    void setConnectionState(bool connected, const QString& description);
    // 追加串口接收数据。
    void appendReceivedData(const QByteArray& data);
    // 追加串口发送数据。
    void appendSentData(const QByteArray& data);
    // 显示操作错误。
    void showError(const QString& message);

private slots:
    // 处理打开或关闭按钮点击事件。
    void handleOpenCloseClicked();
    // 处理发送按钮点击事件。
    void handleSendClicked();
    // 清空通讯记录。
    void clearTraffic();

private:
    // 创建页面控件和布局。
    void buildUi();
    // 解析发送输入框内容。
    QByteArray parseTransmitData(bool& valid) const;
    // 追加一条带时间戳的通讯记录。
    void appendTraffic(const QString& direction, const QByteArray& data);

    ElaComboBox* _portComboBox{nullptr};
    ElaComboBox* _baudComboBox{nullptr};
    ElaPushButton* _refreshButton{nullptr};
    ElaPushButton* _openCloseButton{nullptr};
    ElaPlainTextEdit* _trafficView{nullptr};
    ElaLineEdit* _transmitEdit{nullptr};
    ElaCheckBox* _hexModeCheckBox{nullptr};
    ElaPushButton* _sendButton{nullptr};
    ElaPushButton* _clearButton{nullptr};
    ElaText* _statusText{nullptr};
    bool _connected{false};
};

#endif // MONITORPAGE_H
