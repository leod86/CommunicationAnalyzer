#ifndef MONITORPAGE_H
#define MONITORPAGE_H

#include <QByteArray>
#include <QColor>
#include <QDateTime>
#include <QList>
#include <QStringList>
#include "ElaScrollPage.h"

class ElaComboBox;
class ElaIconButton;
class ElaLineEdit;
class ElaListView;
class ElaPivot;
class ElaPushButton;
class ElaText;
class ElaToolBar;
class ElaToggleButton;
class ElaToolButton;
class ColorSwatchButton;
class QFile;
class QStandardItemModel;
class QSplitter;
class QStackedWidget;
class QTimer;
class QValidator;
class QVBoxLayout;
class QSettings;
class TrafficTextEdit;

class MonitorPage final : public ElaScrollPage
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
    // 切换右侧发送和报文搜索页面。
    void handleSidePanelPageChanged(int index);
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
    // 根据搜索输入更新接收报文的命中背景和Ela结果列表。
    void updateTrafficSearch();

private:
    // 单条通讯记录，保存方向、时间和原始字节数据。
    struct TrafficRecord
    {
        bool received{false};
        QDateTime timestamp;
        QByteArray data;
    };

    // 单条通讯记录的文本和HEX显示内容。
    struct TrafficDisplayContent
    {
        QString text;
        QString hex;
    };

    // 单条快捷发送配置，包含发送类型、内容输入框和发送按钮。
    struct QuickSendItem
    {
        ElaComboBox* modeComboBox{nullptr};
        ElaLineEdit* contentEdit{nullptr};
        ElaToggleButton* sendButton{nullptr};
        QValidator* hexValidator{nullptr};
    };

    // 协议解析中的一个匹配字段及其编辑控件。
    struct ProtocolField
    {
        QString name;
        QByteArray pattern;
        int length{0};
        QColor color;
        ElaLineEdit* nameEdit{nullptr};
        ElaLineEdit* hexEdit{nullptr};
        ElaLineEdit* lengthEdit{nullptr};
        ColorSwatchButton* colorButton{nullptr};
        ElaIconButton* removeButton{nullptr};
        QWidget* rowWidget{nullptr};
    };

    // 一套可编辑并应用到报文的协议解析配置。
    struct ProtocolConfig
    {
        QString name;
        QList<ProtocolField> fields;
    };

    // 创建页面控件和布局。
    void buildUi();
    // 创建放置在主窗口顶部的串口配置工具栏。
    ElaToolBar* buildSerialToolBar();
    // 创建右侧多字符串快捷发送配置区域。
    QWidget* buildMultiSendPanel();
    // 创建协议解析配置页面。
    QWidget* buildProtocolPanel();
    // 重建配置选择器并选中指定配置。
    void refreshProtocolConfigComboBox(int selectedConfigIndex);
    // 按当前配置重建字段编辑行。
    void rebuildProtocolFieldRows();
    // 新建一个协议配置并切换为当前配置。
    void addProtocolConfig();
    // 删除当前协议配置。
    void removeCurrentProtocolConfig();
    // 在当前配置末尾新增一个字段。
    void addProtocolField();
    // 删除指定字段并重建列表。
    void removeProtocolField(int fieldIndex);
    // 弹出颜色选择器修改指定字段的颜色。
    void chooseProtocolFieldColor(int fieldIndex);
    // 处理配置选择器切换。
    void handleProtocolConfigChanged();
    // 请求防抖重绘历史报文。
    void scheduleProtocolRerender();
    // 计算一帧数据中每个字节命中的字段序号，未命中为-1。
    QList<int> computeProtocolByteColors(const QByteArray& data) const;
    // 获取当前配置中全部字段的颜色。
    QList<QColor> activeProtocolFieldColors() const;
    // 获取当前配置中指定序号的字段，越界返回空。
    ProtocolField* currentProtocolField(int fieldIndex);
    // 按匹配HEX同步字段长度输入框的取值与可编辑状态。
    void updateProtocolFieldLengthState(ProtocolField& field);
    // 从本地设置恢复协议解析配置。
    void loadProtocolSettings(QSettings& settings);
    // 将协议解析配置写入本地设置。
    void saveProtocolSettings(QSettings& settings);
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
    // 按当前编码选项生成与接收区一致的文本和HEX内容。
    TrafficDisplayContent formatTrafficDisplayContent(const TrafficRecord& record) const;
    // 将通讯记录写入日志文件。
    void writeLog(const TrafficRecord& record);
    // 更新底部状态栏中当前鼠标选中文本的字符数量。
    void updateSelectionLength();
    // 根据连接状态启用或锁定硬件参数。
    void setHardwareControlsEnabled(bool enabled);
    // 将接收窗口滚动到指定搜索结果对应的文档块。
    void locateTrafficSearchResult(int blockId);

    ElaComboBox* _transportComboBox{nullptr};
    ElaComboBox* _portComboBox{nullptr};
    ElaComboBox* _baudComboBox{nullptr};
    ElaComboBox* _dataBitsComboBox{nullptr};
    ElaComboBox* _parityComboBox{nullptr};
    ElaComboBox* _stopBitsComboBox{nullptr};
    ElaLineEdit* _frameTimeoutEdit{nullptr};
    ElaToolBar* _serialToolBar{nullptr};
    ElaIconButton* _refreshButton{nullptr};
    ElaToggleButton* _connectionButton{nullptr};
    ElaComboBox* _dataFormatComboBox{nullptr};
    TrafficTextEdit* _trafficView{nullptr};
    ElaToolButton* _timeButton{nullptr};
    ElaToolButton* _utf8Button{nullptr};
    ElaToolButton* _rxButton{nullptr};
    ElaToolButton* _txButton{nullptr};
    ElaToolButton* _logButton{nullptr};
    ElaToolButton* _pauseScrollButton{nullptr};
    ElaLineEdit* _trafficSearchEdit{nullptr};
    ElaListView* _trafficSearchResults{nullptr};
    QStandardItemModel* _trafficSearchResultsModel{nullptr};
    ElaLineEdit* _textTransmitEdit{nullptr};
    ElaLineEdit* _hexTransmitEdit{nullptr};
    ElaPushButton* _inputModeButton{nullptr};
    ElaComboBox* _checksumComboBox{nullptr};
    ElaComboBox* _suffixComboBox{nullptr};
    ElaToggleButton* _sendButton{nullptr};
    ElaToggleButton* _continuousSendButton{nullptr};
    ElaLineEdit* _sendIntervalEdit{nullptr};
    ElaPivot* _sidePanelPivot{nullptr};
    QStackedWidget* _sidePanelStackedWidget{nullptr};
    QSplitter* _contentSplitter{nullptr};
    ElaText* _statusText{nullptr};
    ElaText* _selectionLengthText{nullptr};
    ElaText* _transferText{nullptr};
    QFile* _logFile{nullptr};
    QTimer* _rateTimer{nullptr};
    QTimer* _continuousSendTimer{nullptr};
    QTimer* _trafficSearchTimer{nullptr};
    QList<TrafficRecord> _trafficRecords;
    QList<QuickSendItem> _quickSendItems;
    ElaComboBox* _protocolConfigComboBox{nullptr};
    ElaIconButton* _protocolAddConfigButton{nullptr};
    ElaIconButton* _protocolRemoveConfigButton{nullptr};
    ElaPushButton* _protocolAddFieldButton{nullptr};
    QWidget* _protocolFieldListWidget{nullptr};
    QVBoxLayout* _protocolFieldListLayout{nullptr};
    QTimer* _protocolRerenderTimer{nullptr};
    QList<ProtocolConfig> _protocolConfigs;
    int _nextProtocolConfigNumber{1};
    QString _preferredPortName;
    QString _trafficSearchQuery;
    int _nextTrafficSearchBlockId{0};
    bool _connected{false};
    quint64 _rxBytes{0};
    quint64 _txBytes{0};
    quint64 _lastRxBytes{0};
    quint64 _lastTxBytes{0};
};

#endif // MONITORPAGE_H
