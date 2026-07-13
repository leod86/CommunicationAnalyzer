#ifndef MONITORPAGE_H
#define MONITORPAGE_H

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QStringList>
#include <QWidget>

class ElaComboBox;
class ElaCheckBox;
class ElaIconButton;
class ElaLineEdit;
class ElaPushButton;
class ElaText;
class ElaToolBar;
class ElaToggleSwitch;
class ElaToolButton;
class QFile;
class QTimer;
class QValidator;
class TrafficTextEdit;

class MonitorPage final : public QWidget
{
    Q_OBJECT

public:
    // 创建通讯监视页面。
    explicit MonitorPage(QWidget* parent = nullptr);
    // 释放通讯监视页面。
    ~MonitorPage() override;
    // 获取当前断帧等待时间。
    int frameTimeout() const;
    // 获取放置在主窗口顶部的串口配置工具栏。
    ElaToolBar* serialToolBar() const;

signals:
    // 请求刷新可用串口。
    void refreshPortsRequested();
    // 请求使用完整参数打开指定串口。
    void openPortRequested(const QString& portName, qint32 baudRate, int dataBits, int parity, int stopBits);
    // 请求关闭当前串口。
    void closePortRequested();
    // 请求发送字节数据。
    void sendDataRequested(const QByteArray& data);
    // 通知串口控制器更新断帧等待时间。
    void frameTimeoutChanged(int milliseconds);

public slots:
    // 更新页面中的串口列表和设备描述。
    void updatePorts(const QStringList& portNames, const QStringList& descriptions);
    // 更新串口连接状态。
    void setConnectionState(bool connected, const QString& description);
    // 追加串口接收数据。
    void appendReceivedData(const QByteArray& data);
    // 追加串口发送数据。
    void appendSentData(const QByteArray& data);
    // 显示操作错误。
    void showError(const QString& message);

private slots:
    // 处理连接开关状态变化。
    void handleConnectionToggled(bool checked);
    // 处理发送按钮点击事件。
    void handleSendClicked();
    // 文本输入变化后同步生成HEX内容。
    void handleTextTransmitChanged(const QString& text);
    // HEX输入合法时同步生成文本内容。
    void handleHexTransmitChanged(const QString& text);
    // 校验并应用文本输入的断帧时间。
    void handleFrameTimeoutEditingFinished();
    // 展开或折叠右侧多字符串发送配置区。
    void handleMultiSendToggled(bool expanded);
    // 开启或停止主发送区的连续发送。
    void handleContinuousSendToggled(bool checked);
    // 校验并应用连续发送间隔。
    void handleSendIntervalEditingFinished();
    // 清空通讯记录和累计统计。
    void clearTraffic();
    // 根据工具栏选项重新生成数据显示。
    void renderTraffic();
    // 开启或关闭日志文件记录。
    void handleLogToggled(bool checked);
    // 暂停或恢复接收窗口自动滚动。
    void handlePauseScrollToggled(bool paused);
    // 更新每秒收发速率。
    void updateTransferRate();

private:
    // 单条通讯记录，保存方向、时间和原始字节数据。
    struct TrafficRecord
    {
        bool received{false};
        QDateTime timestamp;
        QByteArray data;
    };

    // 单条快捷发送配置，包含发送类型、内容输入框和发送按钮。
    struct QuickSendItem
    {
        ElaComboBox* modeComboBox{nullptr};
        ElaLineEdit* contentEdit{nullptr};
        ElaPushButton* sendButton{nullptr};
        QValidator* hexValidator{nullptr};
    };

    // 创建页面控件和布局。
    void buildUi();
    // 创建放置在主窗口顶部的串口配置工具栏。
    ElaToolBar* buildSerialToolBar();
    // 创建右侧多字符串快捷发送配置区域。
    QWidget* buildMultiSendPanel();
    // 从本地设置中恢复用户配置。
    void loadSettings();
    // 将当前用户配置保存到本地设置。
    void saveSettings();
    // 建立配置控件变化后的自动保存连接。
    void connectSettingsSignals();
    // 解析发送输入框内容并追加校验与结束符。
    QByteArray parseTransmitData(bool& valid) const;
    // 按文本或HEX模式解析指定输入内容。
    QByteArray parseTransmitInput(const QString& input, bool hexMode, bool& valid) const;
    // 将HEX输入按两个字符一组格式化并保持光标所在字节位置。
    void formatHexLineEdit(ElaLineEdit* lineEdit) const;
    // 发送指定序号的多字符串预设内容。
    void sendQuickItem(int index);
    // 校验并更新指定快捷发送项的输入状态。
    void updateQuickSendItemState(int index);
    // 获取当前连续发送间隔。
    int sendInterval() const;
    // 根据选择的算法向发送数据追加校验值。
    void appendChecksum(QByteArray& data) const;
    // 追加并显示一条通讯记录。
    void appendTraffic(bool received, const QByteArray& data);
    // 使用不同背景样式向接收区插入一条通讯记录。
    void appendFormattedTraffic(const TrafficRecord& record);
    // 清除文本中的 ANSI 控制序列。
    QString stripAnsiSequences(const QString& text) const;
    // 将通讯记录写入日志文件。
    void writeLog(const TrafficRecord& record);
    // 根据连接状态启用或锁定硬件参数。
    void setHardwareControlsEnabled(bool enabled);

    ElaComboBox* _transportComboBox{nullptr};
    ElaComboBox* _portComboBox{nullptr};
    ElaComboBox* _baudComboBox{nullptr};
    ElaComboBox* _dataBitsComboBox{nullptr};
    ElaComboBox* _parityComboBox{nullptr};
    ElaComboBox* _stopBitsComboBox{nullptr};
    ElaLineEdit* _frameTimeoutEdit{nullptr};
    ElaToolBar* _serialToolBar{nullptr};
    ElaIconButton* _refreshButton{nullptr};
    ElaToggleSwitch* _connectionSwitch{nullptr};
    ElaText* _connectionText{nullptr};
    ElaComboBox* _dataFormatComboBox{nullptr};
    TrafficTextEdit* _trafficView{nullptr};
    ElaToolButton* _timeButton{nullptr};
    ElaToolButton* _utf8Button{nullptr};
    ElaToolButton* _rxButton{nullptr};
    ElaToolButton* _txButton{nullptr};
    ElaToolButton* _ansiButton{nullptr};
    ElaToolButton* _logButton{nullptr};
    ElaToolButton* _pauseScrollButton{nullptr};
    ElaLineEdit* _textTransmitEdit{nullptr};
    ElaLineEdit* _hexTransmitEdit{nullptr};
    ElaPushButton* _inputModeButton{nullptr};
    ElaComboBox* _checksumComboBox{nullptr};
    ElaComboBox* _suffixComboBox{nullptr};
    ElaPushButton* _sendButton{nullptr};
    ElaPushButton* _multiSendToggleButton{nullptr};
    ElaCheckBox* _continuousSendCheckBox{nullptr};
    ElaLineEdit* _sendIntervalEdit{nullptr};
    QWidget* _multiSendPanel{nullptr};
    ElaText* _statusText{nullptr};
    ElaText* _transferText{nullptr};
    QFile* _logFile{nullptr};
    QTimer* _rateTimer{nullptr};
    QTimer* _continuousSendTimer{nullptr};
    QList<TrafficRecord> _trafficRecords;
    QList<QuickSendItem> _quickSendItems;
    QString _preferredPortName;
    bool _connected{false};
    quint64 _rxBytes{0};
    quint64 _txBytes{0};
    quint64 _lastRxBytes{0};
    quint64 _lastTxBytes{0};
};

#endif // MONITORPAGE_H
