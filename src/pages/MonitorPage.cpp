#include "pages/MonitorPage.h"

#include "ElaCheckBox.h"
#include "ElaComboBox.h"
#include "ElaIconButton.h"
#include "ElaLineEdit.h"
#include "ElaListView.h"
#include "ElaMessageBar.h"
#include "ElaPivot.h"
#include "ElaPushButton.h"
#include "ElaScrollArea.h"
#include "ElaText.h"
#include "ElaToolBar.h"
#include "ElaToggleSwitch.h"
#include "ElaToolButton.h"
#include "widgets/TrafficTextEdit.h"

#include <algorithm>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QComboBox>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QResizeEvent>
#include <QScrollBar>
#include <QSerialPort>
#include <QSet>
#include <QSettings>
#include <QSignalBlocker>
#include <QShowEvent>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>

namespace
{
// 流式控件位置，记录控件及其从左到右的排列顺序。
struct ResponsiveItemPosition
{
    QWidget* widget{nullptr};
    int order{0};
};

class ResponsiveRowWidget final : public QWidget
{
public:
    // 创建可根据可用宽度自然换行的控件容器。
    explicit ResponsiveRowWidget(QWidget* parent = nullptr);
    // 注册控件及其从左到右的排列顺序。
    void addResponsiveWidget(QWidget* widget, int order);
    // 返回不锁定横向宽度的推荐尺寸。
    QSize sizeHint() const override;
    // 返回不锁定横向宽度的最小尺寸。
    QSize minimumSizeHint() const override;

protected:
    // 在可用宽度变化后重新计算自然断行位置。
    void resizeEvent(QResizeEvent* event) override;
    // 在首次显示时按最终宽度校正自然断行位置。
    void showEvent(QShowEvent* event) override;

private:
    // 按当前可用宽度依次放置控件并在空间不足时换行。
    void updateResponsiveLayout(int availableWidth);

    QVBoxLayout* _rootLayout{nullptr};
    QList<ResponsiveItemPosition> _items;
};

/*****************************************************
函数名称：ResponsiveRowWidget::ResponsiveRowWidget(QWidget* parent)
入口参数：parent为父窗口
出口参数：无
函数功能：创建按可用宽度自然换行的响应式控件容器
*****************************************************/
ResponsiveRowWidget::ResponsiveRowWidget(QWidget* parent)
    : QWidget(parent)
    , _rootLayout(new QVBoxLayout(this))
{
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Minimum);
    _rootLayout->setContentsMargins(0, 0, 0, 0);
    _rootLayout->setSpacing(5);
}

/*****************************************************
函数名称：void ResponsiveRowWidget::addResponsiveWidget(QWidget* widget, int order)
入口参数：widget为控件，order为控件从左到右的排列顺序
出口参数：无
函数功能：注册流式控件并按照当前宽度重新计算断行位置
*****************************************************/
void ResponsiveRowWidget::addResponsiveWidget(QWidget* widget, int order)
{
    QSizePolicy sizePolicy = widget->sizePolicy();
    sizePolicy.setHorizontalPolicy(QSizePolicy::Maximum);
    widget->setSizePolicy(sizePolicy);
    _items.append({widget, order});
    updateResponsiveLayout(width());
}

/*****************************************************
函数名称：QSize ResponsiveRowWidget::sizeHint() const
入口参数：无
出口参数：不限制横向宽度的推荐尺寸
函数功能：避免当前行宽度反向锁定父布局，使流式工具栏可以随分割器缩窄
*****************************************************/
QSize ResponsiveRowWidget::sizeHint() const
{
    QSize hint = QWidget::sizeHint();
    hint.setWidth(0);
    return hint;
}

/*****************************************************
函数名称：QSize ResponsiveRowWidget::minimumSizeHint() const
入口参数：无
出口参数：不限制横向宽度的最小尺寸
函数功能：仅保留流式工具栏的纵向高度要求，不向父布局传递当前行的最小宽度
*****************************************************/
QSize ResponsiveRowWidget::minimumSizeHint() const
{
    QSize hint = QWidget::minimumSizeHint();
    hint.setWidth(0);
    return hint;
}

/*****************************************************
函数名称：void ResponsiveRowWidget::resizeEvent(QResizeEvent* event)
入口参数：event为尺寸变化事件
出口参数：无
函数功能：容器宽度变化时按照控件实际宽度重新计算自然断行位置
*****************************************************/
void ResponsiveRowWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updateResponsiveLayout(event->size().width());
}

/*****************************************************
函数名称：void ResponsiveRowWidget::showEvent(QShowEvent* event)
入口参数：event为控件显示事件
出口参数：无
函数功能：控件首次显示时根据最终可用宽度校正自然断行位置
*****************************************************/
void ResponsiveRowWidget::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    QTimer::singleShot(0, this, [this] {
        updateResponsiveLayout(width());
    });
}

/*****************************************************
函数名称：void ResponsiveRowWidget::updateResponsiveLayout(int availableWidth)
入口参数：availableWidth为容器当前可用宽度
出口参数：无
函数功能：按控件自然宽度依次排列并在当前行空间不足时换行
*****************************************************/
void ResponsiveRowWidget::updateResponsiveLayout(int availableWidth)
{
    while (QLayoutItem* rowItem = _rootLayout->takeAt(0))
    {
        QLayout* rowLayout = rowItem->layout();
        if (rowLayout)
        {
            while (QLayoutItem* childItem = rowLayout->takeAt(0))
            {
                delete childItem;
            }
            delete rowLayout;
        }
        else
        {
            delete rowItem;
        }
    }

    QList<ResponsiveItemPosition> orderedItems = _items;
    std::sort(orderedItems.begin(), orderedItems.end(), [](const ResponsiveItemPosition& left,
                                                           const ResponsiveItemPosition& right) {
        return left.order < right.order;
    });

    constexpr int itemSpacing = 5;
    const int usableWidth = qMax(1, availableWidth);
    QList<QList<QWidget*>> rows;
    QList<QWidget*> currentRow;
    int currentRowWidth = 0;
    for (const ResponsiveItemPosition& item : orderedItems)
    {
        const int widgetWidth = qMax(item.widget->minimumWidth(),
                                     item.widget->isVisible() ? item.widget->width()
                                                              : item.widget->sizeHint().width());
        const int requiredWidth = widgetWidth + (currentRow.isEmpty() ? 0 : itemSpacing);
        if (!currentRow.isEmpty() && currentRowWidth + requiredWidth > usableWidth)
        {
            rows.append(currentRow);
            currentRow.clear();
            currentRowWidth = 0;
        }

        currentRow.append(item.widget);
        currentRowWidth += widgetWidth + (currentRow.size() == 1 ? 0 : itemSpacing);
    }
    if (!currentRow.isEmpty())
    {
        rows.append(currentRow);
    }

    for (const QList<QWidget*>& row : rows)
    {
        auto* rowLayout = new QHBoxLayout();
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(itemSpacing);
        for (QWidget* widget : row)
        {
            rowLayout->addWidget(widget);
        }
        rowLayout->addStretch(1);
        _rootLayout->addLayout(rowLayout);
    }
    updateGeometry();
}
}

/*****************************************************
函数名称：MonitorPage::MonitorPage(QWidget* parent)
入口参数：parent为父窗口
出口参数：无
函数功能：初始化通讯监视页面
*****************************************************/
MonitorPage::MonitorPage(QWidget* parent)
    : ElaScrollPage(parent)
{
    buildUi();
}

/*****************************************************
函数名称：MonitorPage::~MonitorPage()
入口参数：无
出口参数：无
函数功能：释放通讯监视页面和日志文件
*****************************************************/
MonitorPage::~MonitorPage()
{
    saveSettings();
    if (_logFile && _logFile->isOpen())
    {
        _logFile->close();
    }
}

/*****************************************************
函数名称：int MonitorPage::frameTimeout() const
入口参数：无
出口参数：当前断帧等待时间，单位毫秒
函数功能：向串口控制器提供界面保存的断帧配置
*****************************************************/
int MonitorPage::frameTimeout() const
{
    bool valid = false;
    const int milliseconds = _frameTimeoutEdit->text().toInt(&valid);
    return valid ? qBound(1, milliseconds, 5000) : 20;
}

/*****************************************************
函数名称：ElaToolBar* MonitorPage::serialToolBar() const
入口参数：无
出口参数：主窗口顶部串口配置工具栏
函数功能：向主窗口提供页面创建并维护的串口配置工具栏
*****************************************************/
ElaToolBar* MonitorPage::serialToolBar() const
{
    return _serialToolBar;
}

/*****************************************************
函数名称：int MonitorPage::sendInterval() const
入口参数：无
出口参数：当前连续发送间隔，单位毫秒
函数功能：读取并限制连续发送间隔到有效范围
*****************************************************/
int MonitorPage::sendInterval() const
{
    bool valid = false;
    const int milliseconds = _sendIntervalEdit->text().toInt(&valid);
    return valid ? qBound(10, milliseconds, 600000) : 1000;
}

/*****************************************************
函数名称：void MonitorPage::buildUi()
入口参数：无
出口参数：无
函数功能：创建参考界面中的配置、显示、发送和状态区域
*****************************************************/
void MonitorPage::buildUi()
{
    setWindowTitle(QStringLiteral("串口监视"));
    setTitleVisible(false);
    setContentsMargins(0, 0, 0, 0);

    // 串口配置工具栏由主窗口放置在页面上方。
    _serialToolBar = buildSerialToolBar();

    auto* pageContent = new QWidget(this);
    pageContent->setWindowTitle(QStringLiteral("串口监视"));
    pageContent->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    auto* pageLayout = new QVBoxLayout(pageContent);
    pageLayout->setSizeConstraint(QLayout::SetNoConstraint);
    pageLayout->setContentsMargins(0, 8, 12, 8);
    pageLayout->setSpacing(8);

    auto* displayToolbarWidget = new ResponsiveRowWidget(this);
    QFont compactControlFont = font();
    if (compactControlFont.pointSizeF() > 0)
    {
        compactControlFont.setPointSizeF(compactControlFont.pointSizeF() * 0.85);
    }
    else if (compactControlFont.pixelSize() > 0)
    {
        compactControlFont.setPixelSize(qMax(1, qRound(compactControlFont.pixelSize() * 0.85)));
    }

    _dataFormatComboBox = new ElaComboBox(this);
    _dataFormatComboBox->addItem(QStringLiteral("RawData-文本"), 0);
    _dataFormatComboBox->addItem(QStringLiteral("RawData-HEX"), 1);
    _dataFormatComboBox->addItem(QStringLiteral("RawData-HEX对照"), 2);
    _dataFormatComboBox->setFixedSize(144, 30);
    _dataFormatComboBox->setFont(compactControlFont);
    _dataFormatComboBox->setToolTip(QStringLiteral("选择固定RawData显示方式；自定义解析显示将在后续加入"));

    _timeButton = new ElaToolButton(this);
    _timeButton->setText(QStringLiteral("Time"));
    _timeButton->setElaIcon(ElaIconType::Clock);
    _timeButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    _timeButton->setCheckable(true);
    _timeButton->setChecked(true);
    _timeButton->setFixedHeight(30);
    _timeButton->setFont(compactControlFont);

    _utf8Button = new ElaToolButton(this);
    _utf8Button->setText(QStringLiteral("UTF-8"));
    _utf8Button->setElaIcon(ElaIconType::FileLines);
    _utf8Button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    _utf8Button->setCheckable(true);
    _utf8Button->setChecked(true);
    _utf8Button->setFixedHeight(30);
    _utf8Button->setFont(compactControlFont);

    _rxButton = new ElaToolButton(this);
    _rxButton->setText(QStringLiteral("RX"));
    _rxButton->setElaIcon(ElaIconType::Eye);
    _rxButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    _rxButton->setCheckable(true);
    _rxButton->setChecked(true);
    _rxButton->setFixedHeight(30);
    _rxButton->setFont(compactControlFont);

    _txButton = new ElaToolButton(this);
    _txButton->setText(QStringLiteral("TX"));
    _txButton->setElaIcon(ElaIconType::Eye);
    _txButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    _txButton->setCheckable(true);
    _txButton->setChecked(true);
    _txButton->setFixedHeight(30);
    _txButton->setFont(compactControlFont);

    _ansiButton = new ElaToolButton(this);
    _ansiButton->setText(QStringLiteral("ANSI"));
    _ansiButton->setElaIcon(ElaIconType::Terminal);
    _ansiButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    _ansiButton->setCheckable(true);
    _ansiButton->setFixedHeight(30);
    _ansiButton->setFont(compactControlFont);
    _ansiButton->setToolTip(QStringLiteral("解析并隐藏 ANSI 控制序列"));

    _logButton = new ElaToolButton(this);
    _logButton->setText(QStringLiteral("Log"));
    _logButton->setElaIcon(ElaIconType::Circle);
    _logButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    _logButton->setCheckable(true);
    _logButton->setFixedHeight(30);
    _logButton->setFont(compactControlFont);
    _logButton->setToolTip(QStringLiteral("将后续收发数据写入日志文件"));

    _pauseScrollButton = new ElaToolButton(this);
    _pauseScrollButton->setText(QStringLiteral("暂停滚动"));
    _pauseScrollButton->setElaIcon(ElaIconType::Pause);
    _pauseScrollButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    _pauseScrollButton->setCheckable(true);
    _pauseScrollButton->setFixedHeight(30);
    _pauseScrollButton->setFont(compactControlFont);
    _pauseScrollButton->setToolTip(QStringLiteral("锁定当前滚动位置，收发数据仍继续追加"));

    auto* receiveSeparator = new QFrame(this);
    receiveSeparator->setFrameShape(QFrame::VLine);
    receiveSeparator->setFrameShadow(QFrame::Sunken);
    receiveSeparator->setFixedHeight(22);

    auto* frameTimeoutLabel = new ElaText(QStringLiteral("断帧(ms)"), this);
    frameTimeoutLabel->setTextPixelSize(11);

    _frameTimeoutEdit = new ElaLineEdit(this);
    _frameTimeoutEdit->setText(QStringLiteral("20"));
    _frameTimeoutEdit->setValidator(new QIntValidator(1, 5000, _frameTimeoutEdit));
    _frameTimeoutEdit->setFixedSize(58, 30);
    _frameTimeoutEdit->setFont(compactControlFont);
    _frameTimeoutEdit->setAlignment(Qt::AlignCenter);
    _frameTimeoutEdit->setToolTip(QStringLiteral("连续接收数据静默指定时间后合并为一帧"));

    auto* clearButton = new ElaIconButton(ElaIconType::Eraser, 15, 31, 27, this);
    clearButton->setToolTip(QStringLiteral("清空显示和统计"));

    displayToolbarWidget->addResponsiveWidget(_dataFormatComboBox, 0);
    displayToolbarWidget->addResponsiveWidget(_timeButton, 1);
    displayToolbarWidget->addResponsiveWidget(_utf8Button, 2);
    displayToolbarWidget->addResponsiveWidget(_rxButton, 3);
    displayToolbarWidget->addResponsiveWidget(_txButton, 4);
    displayToolbarWidget->addResponsiveWidget(_ansiButton, 5);
    displayToolbarWidget->addResponsiveWidget(_logButton, 6);
    displayToolbarWidget->addResponsiveWidget(_pauseScrollButton, 7);
    displayToolbarWidget->addResponsiveWidget(receiveSeparator, 8);
    displayToolbarWidget->addResponsiveWidget(frameTimeoutLabel, 9);
    displayToolbarWidget->addResponsiveWidget(_frameTimeoutEdit, 10);
    displayToolbarWidget->addResponsiveWidget(clearButton, 11);

    _trafficView = new TrafficTextEdit(this);
    _trafficView->setReadOnly(true);
    _trafficView->setMinimumHeight(120);
    _trafficView->setPlaceholderText(QStringLiteral("连接设备后，收发数据将在这里显示"));
    _trafficView->document()->setMaximumBlockCount(20000);

    auto* singleSendPanel = new QWidget(this);
    auto* transmitLayout = new QGridLayout(singleSendPanel);
    transmitLayout->setContentsMargins(0, 0, 2, 0);
    transmitLayout->setHorizontalSpacing(7);
    transmitLayout->setVerticalSpacing(7);

    auto* textInputLabel = new ElaText(QStringLiteral("文本"), this);
    textInputLabel->setTextPixelSize(11);

    auto* hexInputLabel = new ElaText(QStringLiteral("HEX"), this);
    hexInputLabel->setTextPixelSize(11);

    _textTransmitEdit = new ElaLineEdit(this);
    _textTransmitEdit->setPlaceholderText(QStringLiteral("输入文本数据，例如：Spd_Cmd:0.0"));
    _textTransmitEdit->setClearButtonEnabled(true);
    _textTransmitEdit->setFixedHeight(30);
    _textTransmitEdit->setFont(compactControlFont);

    _hexTransmitEdit = new ElaLineEdit(this);
    _hexTransmitEdit->setPlaceholderText(QStringLiteral("输入HEX数据，例如：01 03 00 00 00 02"));
    _hexTransmitEdit->setClearButtonEnabled(true);
    _hexTransmitEdit->setFixedHeight(30);
    _hexTransmitEdit->setFont(compactControlFont);
    _hexTransmitEdit->setValidator(new QRegularExpressionValidator(
        QRegularExpression(QStringLiteral("^[0-9A-Fa-f\\s]*$")), _hexTransmitEdit));

    _checksumComboBox = new ElaComboBox(this);
    _checksumComboBox->addItem(QStringLiteral("none"), 0);
    _checksumComboBox->addItem(QStringLiteral("XOR8"), 1);
    _checksumComboBox->addItem(QStringLiteral("SUM8"), 2);
    _checksumComboBox->addItem(QStringLiteral("CRC16"), 3);
    _checksumComboBox->setMinimumWidth(112);
    _checksumComboBox->setFixedHeight(30);
    _checksumComboBox->setFont(compactControlFont);
    _checksumComboBox->setToolTip(QStringLiteral("发送前追加校验值"));

    _suffixComboBox = new ElaComboBox(this);
    _suffixComboBox->addItem(QStringLiteral("none"), QByteArray());
    _suffixComboBox->addItem(QStringLiteral("\\n"), QByteArray("\n"));
    _suffixComboBox->addItem(QStringLiteral("\\r\\n"), QByteArray("\r\n"));
    _suffixComboBox->addItem(QStringLiteral("\\r"), QByteArray("\r"));
    _suffixComboBox->setCurrentIndex(1);
    _suffixComboBox->setMinimumWidth(88);
    _suffixComboBox->setFixedHeight(30);
    _suffixComboBox->setFont(compactControlFont);
    _suffixComboBox->setToolTip(QStringLiteral("发送结束符"));

    _inputModeButton = new ElaPushButton(QStringLiteral("ABC"), this);
    _inputModeButton->setCheckable(true);
    _inputModeButton->setFixedSize(65, 30);
    _inputModeButton->setFont(compactControlFont);
    _inputModeButton->setToolTip(QStringLiteral("当前发送类型为文本，点击切换为HEX"));

    _sendButton = new ElaPushButton(QStringLiteral("Send"), this);
    _sendButton->setFixedSize(65, 30);
    _sendButton->setFont(compactControlFont);
    _sendButton->setEnabled(false);

    auto* checksumLabel = new ElaText(QStringLiteral("校验"), this);
    checksumLabel->setTextPixelSize(11);

    auto* suffixLabel = new ElaText(QStringLiteral("结束符"), this);
    suffixLabel->setTextPixelSize(11);

    auto* inputModeLabel = new ElaText(QStringLiteral("模式"), this);
    inputModeLabel->setTextPixelSize(11);

    _continuousSendCheckBox = new ElaCheckBox(QStringLiteral("连续发送"), this);
    _continuousSendCheckBox->setFixedHeight(30);
    _continuousSendCheckBox->setFont(compactControlFont);
    _continuousSendCheckBox->setEnabled(false);

    auto* sendIntervalLabel = new ElaText(QStringLiteral("间隔(ms)"), this);
    sendIntervalLabel->setTextPixelSize(11);

    _sendIntervalEdit = new ElaLineEdit(this);
    _sendIntervalEdit->setText(QStringLiteral("1000"));
    _sendIntervalEdit->setValidator(new QIntValidator(10, 600000, _sendIntervalEdit));
    _sendIntervalEdit->setFixedSize(61, 30);
    _sendIntervalEdit->setFont(compactControlFont);
    _sendIntervalEdit->setAlignment(Qt::AlignCenter);
    _sendIntervalEdit->setToolTip(QStringLiteral("连续发送间隔，范围10～600000毫秒"));

    transmitLayout->addWidget(textInputLabel, 0, 0);
    transmitLayout->addWidget(_textTransmitEdit, 0, 1);
    transmitLayout->addWidget(hexInputLabel, 1, 0);
    transmitLayout->addWidget(_hexTransmitEdit, 1, 1);
    transmitLayout->addWidget(checksumLabel, 2, 0);
    transmitLayout->addWidget(_checksumComboBox, 2, 1);
    transmitLayout->addWidget(suffixLabel, 3, 0);
    transmitLayout->addWidget(_suffixComboBox, 3, 1);
    transmitLayout->addWidget(inputModeLabel, 4, 0);
    transmitLayout->addWidget(_inputModeButton, 4, 1, Qt::AlignLeft);
    transmitLayout->addWidget(_continuousSendCheckBox, 5, 1, Qt::AlignLeft);
    transmitLayout->addWidget(sendIntervalLabel, 6, 0);
    transmitLayout->addWidget(_sendIntervalEdit, 6, 1, Qt::AlignLeft);
    transmitLayout->addWidget(_sendButton, 7, 1, Qt::AlignLeft);
    transmitLayout->setColumnStretch(1, 1);
    transmitLayout->setRowStretch(8, 1);

    auto* statusLayout = new QHBoxLayout();
    statusLayout->setContentsMargins(12, 0, 4, 0);
    _statusText = new ElaText(QStringLiteral("未连接"), this);
    _statusText->setTextPixelSize(12);
    _transferText = new ElaText(QStringLiteral("↓ 0 B/s  0 B    ↑ 0 B/s  0 B"), this);
    _transferText->setTextPixelSize(12);
    statusLayout->addWidget(_statusText);
    statusLayout->addStretch();
    statusLayout->addWidget(_transferText);

    auto* monitorWidget = new QWidget(this);
    monitorWidget->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    auto* monitorLayout = new QVBoxLayout(monitorWidget);
    monitorLayout->setContentsMargins(0, 0, 0, 0);
    monitorLayout->setSpacing(8);
    monitorLayout->addWidget(displayToolbarWidget);
    monitorLayout->addWidget(_trafficView, 1);
    monitorLayout->addLayout(statusLayout);

    auto* trafficSearchPanel = new QWidget(this);
    auto* trafficSearchLayout = new QVBoxLayout(trafficSearchPanel);
    trafficSearchLayout->setContentsMargins(0, 0, 0, 0);
    trafficSearchLayout->setSpacing(7);

    _trafficSearchEdit = new ElaLineEdit(trafficSearchPanel);
    _trafficSearchEdit->setFixedHeight(30);
    _trafficSearchEdit->setFont(compactControlFont);
    _trafficSearchEdit->setPlaceholderText(QStringLiteral("搜索接收报文"));
    _trafficSearchEdit->setClearButtonEnabled(true);
    _trafficSearchEdit->setToolTip(QStringLiteral("输入关键字搜索，点击下方结果定位到对应报文"));

    _trafficSearchResults = new ElaListView(trafficSearchPanel);
    _trafficSearchResults->setItemHeight(40);
    _trafficSearchResults->setIsTransparent(true);
    _trafficSearchResults->setEditTriggers(QAbstractItemView::NoEditTriggers);
    _trafficSearchResults->setSelectionMode(QAbstractItemView::SingleSelection);
    _trafficSearchResults->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    _trafficSearchResults->setTextElideMode(Qt::ElideRight);
    _trafficSearchResultsModel = new QStandardItemModel(_trafficSearchResults);
    _trafficSearchResults->setModel(_trafficSearchResultsModel);

    trafficSearchLayout->addWidget(_trafficSearchEdit);
    trafficSearchLayout->addWidget(_trafficSearchResults, 1);

    auto* sidePanel = new QFrame(this);
    sidePanel->setFrameShape(QFrame::StyledPanel);
    sidePanel->setMinimumWidth(320);
    auto* sidePanelLayout = new QVBoxLayout(sidePanel);
    sidePanelLayout->setContentsMargins(8, 7, 8, 8);
    sidePanelLayout->setSpacing(7);

    _sidePanelPivot = new ElaPivot(sidePanel);
    _sidePanelPivot->setFixedHeight(34);
    _sidePanelPivot->setTextPixelSize(14);
    _sidePanelPivot->setPivotSpacing(10);
    _sidePanelPivot->setMarkWidth(64);
    _sidePanelPivot->appendPivot(QStringLiteral("单词发送"));
    _sidePanelPivot->appendPivot(QStringLiteral("多字符串发送"));
    _sidePanelPivot->appendPivot(QStringLiteral("报文搜索"));

    _sidePanelStackedWidget = new QStackedWidget(sidePanel);
    _sidePanelStackedWidget->addWidget(singleSendPanel);
    _sidePanelStackedWidget->addWidget(buildMultiSendPanel());
    _sidePanelStackedWidget->addWidget(trafficSearchPanel);
    sidePanelLayout->addWidget(_sidePanelPivot);
    sidePanelLayout->addWidget(_sidePanelStackedWidget, 1);

    _contentSplitter = new QSplitter(Qt::Horizontal, this);
    _contentSplitter->setChildrenCollapsible(false);
    _contentSplitter->setHandleWidth(8);
    _contentSplitter->addWidget(monitorWidget);
    _contentSplitter->addWidget(sidePanel);
    _contentSplitter->setStretchFactor(0, 1);
    _contentSplitter->setStretchFactor(1, 0);
    _contentSplitter->setSizes({820, 320});
    pageLayout->addWidget(_contentSplitter, 1);
    addCentralWidget(pageContent, true, false, 0);

    // 显示开关统一触发历史记录重绘。
    const QList<ElaToolButton*> displayButtons = {
        _timeButton, _utf8Button, _rxButton, _txButton, _ansiButton};
    for (ElaToolButton* button : displayButtons)
    {
        connect(button, &ElaToolButton::toggled, this, &MonitorPage::renderTraffic);
    }
    connect(_dataFormatComboBox, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &MonitorPage::renderTraffic);

    connect(_logButton, &ElaToolButton::toggled, this, &MonitorPage::handleLogToggled);
    connect(_pauseScrollButton, &ElaToolButton::toggled, this, &MonitorPage::handlePauseScrollToggled);
    connect(clearButton, &ElaIconButton::clicked, this, &MonitorPage::clearTraffic);
    connect(_sendButton, &ElaPushButton::clicked, this, &MonitorPage::handleSendClicked);
    connect(_sidePanelPivot, &ElaPivot::pivotClicked,
            this, &MonitorPage::handleSidePanelPageChanged);
    connect(_textTransmitEdit, &ElaLineEdit::textChanged, this, &MonitorPage::handleTextTransmitChanged);
    connect(_hexTransmitEdit, &ElaLineEdit::textChanged, this, &MonitorPage::handleHexTransmitChanged);
    connect(_textTransmitEdit, &ElaLineEdit::returnPressed, this, &MonitorPage::handleSendClicked);
    connect(_hexTransmitEdit, &ElaLineEdit::returnPressed, this, &MonitorPage::handleSendClicked);
    connect(_inputModeButton, &ElaPushButton::toggled, this, [this](bool hexMode) {
        _inputModeButton->setText(hexMode ? QStringLiteral("HEX") : QStringLiteral("ABC"));
        _inputModeButton->setToolTip(hexMode
            ? QStringLiteral("当前发送类型为HEX，点击切换为文本")
            : QStringLiteral("当前发送类型为文本，点击切换为HEX"));
    });
    connect(_continuousSendCheckBox, &ElaCheckBox::toggled,
            this, &MonitorPage::handleContinuousSendToggled);
    connect(_contentSplitter, &QSplitter::splitterMoved, this, [this](int, int) {
        saveSettings();
    });

    _trafficSearchTimer = new QTimer(this);
    _trafficSearchTimer->setSingleShot(true);
    _trafficSearchTimer->setInterval(100);
    connect(_trafficSearchTimer, &QTimer::timeout, this, &MonitorPage::updateTrafficSearch);
    connect(_trafficSearchEdit, &ElaLineEdit::textChanged, this, [this](const QString& text) {
        _trafficSearchQuery = text.trimmed();
        _trafficSearchTimer->start();
    });
    connect(_trafficSearchEdit, &ElaLineEdit::returnPressed, this, [this] {
        if (_trafficSearchResultsModel->rowCount() > 0)
        {
            const QModelIndex firstResult = _trafficSearchResultsModel->index(0, 0);
            _trafficSearchResults->setCurrentIndex(firstResult);
            locateTrafficSearchResult(firstResult.data(Qt::UserRole).toInt());
        }
    });
    connect(_trafficSearchResults, &ElaListView::clicked, this, [this](const QModelIndex& index) {
        locateTrafficSearchResult(index.data(Qt::UserRole).toInt());
    });

    _rateTimer = new QTimer(this);
    _rateTimer->setInterval(1000);
    connect(_rateTimer, &QTimer::timeout, this, &MonitorPage::updateTransferRate);
    _rateTimer->start();

    _continuousSendTimer = new QTimer(this);
    connect(_continuousSendTimer, &QTimer::timeout, this, &MonitorPage::handleSendClicked);

    loadSettings();
    connectSettingsSignals();
}

/*****************************************************
函数名称：ElaToolBar* MonitorPage::buildSerialToolBar()
入口参数：无
出口参数：主窗口顶部串口配置工具栏
函数功能：创建串口参数、刷新和连接控件并加入窗口级工具栏
*****************************************************/
ElaToolBar* MonitorPage::buildSerialToolBar()
{
    auto* toolBar = new ElaToolBar(QStringLiteral("串口配置"), this);
    toolBar->setObjectName(QStringLiteral("SerialConfigurationToolBar"));
    toolBar->setMovable(false);
    toolBar->setFloatable(false);
    toolBar->setAllowedAreas(Qt::TopToolBarArea);
    toolBar->setContextMenuPolicy(Qt::PreventContextMenu);
    toolBar->setMinimumWidth(0);
    toolBar->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    toolBar->setToolBarSpacing(6);

    _transportComboBox = new ElaComboBox(toolBar);
    _transportComboBox->addItem(QStringLiteral("串口"));
    _transportComboBox->setFixedWidth(76);

    _portComboBox = new ElaComboBox(toolBar);
    _portComboBox->setFixedWidth(140);

    _baudComboBox = new ElaComboBox(toolBar);
    _baudComboBox->addItems({QStringLiteral("1200"), QStringLiteral("2400"), QStringLiteral("4800"),
                             QStringLiteral("9600"), QStringLiteral("19200"), QStringLiteral("38400"),
                             QStringLiteral("57600"), QStringLiteral("115200"), QStringLiteral("230400"),
                             QStringLiteral("460800"), QStringLiteral("921600")});
    _baudComboBox->setCurrentText(QStringLiteral("115200"));
    _baudComboBox->setFixedWidth(96);

    _dataBitsComboBox = new ElaComboBox(toolBar);
    _dataBitsComboBox->addItem(QStringLiteral("5"), static_cast<int>(QSerialPort::Data5));
    _dataBitsComboBox->addItem(QStringLiteral("6"), static_cast<int>(QSerialPort::Data6));
    _dataBitsComboBox->addItem(QStringLiteral("7"), static_cast<int>(QSerialPort::Data7));
    _dataBitsComboBox->addItem(QStringLiteral("8"), static_cast<int>(QSerialPort::Data8));
    _dataBitsComboBox->setCurrentIndex(3);
    _dataBitsComboBox->setFixedWidth(72);
    _dataBitsComboBox->setToolTip(QStringLiteral("数据位：5、6、7或8位"));

    _parityComboBox = new ElaComboBox(toolBar);
    _parityComboBox->addItem(QStringLiteral("无校验"), static_cast<int>(QSerialPort::NoParity));
    _parityComboBox->addItem(QStringLiteral("奇校验"), static_cast<int>(QSerialPort::OddParity));
    _parityComboBox->addItem(QStringLiteral("偶校验"), static_cast<int>(QSerialPort::EvenParity));
    _parityComboBox->addItem(QStringLiteral("标记校验"), static_cast<int>(QSerialPort::MarkParity));
    _parityComboBox->addItem(QStringLiteral("空格校验"), static_cast<int>(QSerialPort::SpaceParity));
    _parityComboBox->setFixedWidth(96);
    _parityComboBox->setToolTip(QStringLiteral("校验位：无、奇、偶、标记或空格校验"));

    _stopBitsComboBox = new ElaComboBox(toolBar);
    _stopBitsComboBox->addItem(QStringLiteral("1"), static_cast<int>(QSerialPort::OneStop));
    _stopBitsComboBox->addItem(QStringLiteral("1.5"), static_cast<int>(QSerialPort::OneAndHalfStop));
    _stopBitsComboBox->addItem(QStringLiteral("2"), static_cast<int>(QSerialPort::TwoStop));
    _stopBitsComboBox->setFixedWidth(56);
    _stopBitsComboBox->setToolTip(QStringLiteral("停止位：1、1.5或2位"));

    _refreshButton = new ElaIconButton(ElaIconType::ArrowsRotate, 17, 34, 32, toolBar);
    _refreshButton->setToolTip(QStringLiteral("刷新串口列表"));

    _connectionText = new ElaText(QStringLiteral("连接"), toolBar);
    _connectionText->setTextPixelSize(14);
    _connectionSwitch = new ElaToggleSwitch(toolBar);
    _connectionSwitch->setEnabled(false);

    auto* leadingSpacer = new QWidget(toolBar);
    leadingSpacer->setFixedWidth(7);
    toolBar->addWidget(leadingSpacer);
    toolBar->addWidget(_transportComboBox);
    toolBar->addWidget(_portComboBox);
    toolBar->addWidget(_baudComboBox);
    toolBar->addWidget(_dataBitsComboBox);
    toolBar->addWidget(_parityComboBox);
    toolBar->addWidget(_stopBitsComboBox);
    toolBar->addSeparator();
    toolBar->addWidget(_refreshButton);
    toolBar->addWidget(_connectionText);
    toolBar->addWidget(_connectionSwitch);

    connect(_refreshButton, &ElaIconButton::clicked, this, &MonitorPage::refreshPortsRequested);
    connect(_connectionSwitch, &ElaToggleSwitch::toggled, this, &MonitorPage::handleConnectionToggled);
    return toolBar;
}

/*****************************************************
函数名称：QWidget* MonitorPage::buildMultiSendPanel()
入口参数：无
出口参数：多字符串快捷发送配置区域
函数功能：创建五十组可滚动配置类型、内容并发送的字符串预设
*****************************************************/
QWidget* MonitorPage::buildMultiSendPanel()
{
    auto* panel = new QWidget(this);
    QFont compactControlFont = font();
    if (compactControlFont.pointSizeF() > 0)
    {
        compactControlFont.setPointSizeF(compactControlFont.pointSizeF() * 0.85);
    }
    else if (compactControlFont.pixelSize() > 0)
    {
        compactControlFont.setPixelSize(qMax(1, qRound(compactControlFont.pixelSize() * 0.85)));
    }

    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* scrollArea = new ElaScrollArea(panel);
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setFrameShape(QFrame::NoFrame);

    auto* listWidget = new QWidget(scrollArea);
    auto* listLayout = new QVBoxLayout(listWidget);
    listLayout->setContentsMargins(0, 0, 4, 0);
    listLayout->setSpacing(7);

    constexpr int quickSendItemCount = 50;
    for (int index = 0; index < quickSendItemCount; ++index)
    {
        auto* rowLayout = new QHBoxLayout();
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(5);

        QuickSendItem item;
        item.modeComboBox = new ElaComboBox(listWidget);
        item.modeComboBox->addItem(QStringLiteral("ABC"), 0);
        item.modeComboBox->addItem(QStringLiteral("HEX"), 1);
        item.modeComboBox->setFixedSize(75, 30);
        item.modeComboBox->setFont(compactControlFont);

        item.contentEdit = new ElaLineEdit(listWidget);
        item.contentEdit->setPlaceholderText(QStringLiteral("字符串 %1").arg(index + 1));
        item.contentEdit->setClearButtonEnabled(true);
        item.contentEdit->setFixedHeight(30);
        item.contentEdit->setFont(compactControlFont);
        item.hexValidator = new QRegularExpressionValidator(
            QRegularExpression(QStringLiteral("^[0-9A-Fa-f\\s]*$")), item.contentEdit);

        item.sendButton = new ElaPushButton(QStringLiteral("发送"), listWidget);
        item.sendButton->setFixedSize(48, 30);
        item.sendButton->setFont(compactControlFont);
        item.sendButton->setEnabled(false);

        rowLayout->addWidget(item.modeComboBox);
        rowLayout->addWidget(item.contentEdit, 1);
        rowLayout->addWidget(item.sendButton);
        listLayout->addLayout(rowLayout);

        _quickSendItems.append(item);
        connect(item.sendButton, &ElaPushButton::clicked, this, [this, index]() {
            sendQuickItem(index);
        });
        connect(item.modeComboBox, qOverload<int>(&QComboBox::currentIndexChanged), this, [this, index](int) {
            updateQuickSendItemState(index);
        });
        connect(item.contentEdit, &ElaLineEdit::textChanged, this, [this, index](const QString&) {
            updateQuickSendItemState(index);
        });
    }

    listLayout->addStretch();
    scrollArea->setWidget(listWidget);
    layout->addWidget(scrollArea, 1);
    return panel;
}

/*****************************************************
函数名称：void MonitorPage::loadSettings()
入口参数：无
出口参数：无
函数功能：恢复串口参数、显示选项、解析选项和发送选项
*****************************************************/
void MonitorPage::loadSettings()
{
    QSettings settings(QStringLiteral("CommunicationAnalyzer"), QStringLiteral("CommunicationAnalyzer"));
    _preferredPortName = settings.value(QStringLiteral("hardware/port")).toString();

    const QString baudRate = settings.value(QStringLiteral("hardware/baudRate"), QStringLiteral("115200")).toString();
    const int baudIndex = _baudComboBox->findText(baudRate);
    if (baudIndex >= 0)
    {
        _baudComboBox->setCurrentIndex(baudIndex);
    }

    const auto restoreByData = [](ElaComboBox* comboBox, const QVariant& value) {
        const int index = comboBox->findData(value);
        if (index >= 0)
        {
            comboBox->setCurrentIndex(index);
        }
    };

    restoreByData(_dataBitsComboBox,
                  settings.value(QStringLiteral("hardware/dataBits"), static_cast<int>(QSerialPort::Data8)).toInt());
    restoreByData(_parityComboBox,
                  settings.value(QStringLiteral("hardware/parity"), static_cast<int>(QSerialPort::NoParity)).toInt());
    restoreByData(_stopBitsComboBox,
                  settings.value(QStringLiteral("hardware/stopBits"), static_cast<int>(QSerialPort::OneStop)).toInt());
    const int storedFrameTimeout = settings.value(QStringLiteral("hardware/frameTimeout"), 20).toInt();
    _frameTimeoutEdit->setText(QString::number(qBound(1, storedFrameTimeout, 5000)));

    int displayMode = settings.value(QStringLiteral("display/mode"), -1).toInt();
    if (displayMode < 0 || displayMode > 2)
    {
        displayMode = settings.value(QStringLiteral("display/hexCompare"), false).toBool()
            ? 2
            : (settings.value(QStringLiteral("display/hex"), false).toBool() ? 1 : 0);
    }
    restoreByData(_dataFormatComboBox, displayMode);
    _timeButton->setChecked(settings.value(QStringLiteral("display/time"), true).toBool());
    _utf8Button->setChecked(settings.value(QStringLiteral("display/utf8"), true).toBool());
    _rxButton->setChecked(settings.value(QStringLiteral("display/rx"), true).toBool());
    _txButton->setChecked(settings.value(QStringLiteral("display/tx"), true).toBool());
    _ansiButton->setChecked(settings.value(QStringLiteral("display/ansi"), false).toBool());
    _trafficView->setTextPointSize(
        settings.value(QStringLiteral("display/fontSize"), _trafficView->textPointSize()).toInt());
    _inputModeButton->setChecked(settings.value(QStringLiteral("transmit/hexMode"), false).toBool());
    const int storedSendInterval = settings.value(QStringLiteral("transmit/interval"), 1000).toInt();
    _sendIntervalEdit->setText(QString::number(qBound(10, storedSendInterval, 600000)));

    restoreByData(_checksumComboBox, settings.value(QStringLiteral("transmit/checksum"), 0));
    restoreByData(_suffixComboBox,
                  settings.value(QStringLiteral("transmit/suffix"), QByteArray("\n")));

    for (int index = 0; index < _quickSendItems.size(); ++index)
    {
        QuickSendItem& item = _quickSendItems[index];
        const QString keyPrefix = QStringLiteral("quickSend/%1/").arg(index);
        restoreByData(item.modeComboBox, settings.value(keyPrefix + QStringLiteral("mode"), 0));
        item.contentEdit->setText(settings.value(keyPrefix + QStringLiteral("content")).toString());
    }

    const int legacySendMode = settings.value(QStringLiteral("quickSend/expanded"), false).toBool() ? 1 : 0;
    const int legacySidePanelPage = settings.value(QStringLiteral("layout/sendMode"), legacySendMode).toInt();
    const int sidePanelPage = qBound(
        0, settings.value(QStringLiteral("layout/sidePanelPage"), legacySidePanelPage).toInt(), 2);
    _sidePanelPivot->setCurrentIndex(sidePanelPage);
    _sidePanelStackedWidget->setCurrentIndex(sidePanelPage);
    const QByteArray splitterState = settings.value(QStringLiteral("layout/contentSplitterV2")).toByteArray();
    if (!splitterState.isEmpty() && _contentSplitter)
    {
        _contentSplitter->restoreState(splitterState);
    }
}

/*****************************************************
函数名称：void MonitorPage::saveSettings()
入口参数：无
出口参数：无
函数功能：持久化当前串口、显示、解析和发送配置
*****************************************************/
void MonitorPage::saveSettings()
{
    if (!_baudComboBox)
    {
        return;
    }

    QSettings settings(QStringLiteral("CommunicationAnalyzer"), QStringLiteral("CommunicationAnalyzer"));
    settings.setValue(QStringLiteral("hardware/port"), _preferredPortName);
    settings.setValue(QStringLiteral("hardware/baudRate"), _baudComboBox->currentText());
    settings.setValue(QStringLiteral("hardware/dataBits"), _dataBitsComboBox->currentData());
    settings.setValue(QStringLiteral("hardware/parity"), _parityComboBox->currentData());
    settings.setValue(QStringLiteral("hardware/stopBits"), _stopBitsComboBox->currentData());
    settings.setValue(QStringLiteral("hardware/frameTimeout"), frameTimeout());
    settings.setValue(QStringLiteral("parser/dataFormat"), QStringLiteral("RawData"));
    settings.setValue(QStringLiteral("display/mode"), _dataFormatComboBox->currentData());
    settings.setValue(QStringLiteral("display/time"), _timeButton->isChecked());
    settings.setValue(QStringLiteral("display/utf8"), _utf8Button->isChecked());
    settings.setValue(QStringLiteral("display/rx"), _rxButton->isChecked());
    settings.setValue(QStringLiteral("display/tx"), _txButton->isChecked());
    settings.setValue(QStringLiteral("display/ansi"), _ansiButton->isChecked());
    settings.setValue(QStringLiteral("display/fontSize"), _trafficView->textPointSize());
    settings.remove(QStringLiteral("display/hex"));
    settings.remove(QStringLiteral("display/hexCompare"));
    settings.setValue(QStringLiteral("transmit/hexMode"), _inputModeButton->isChecked());
    settings.setValue(QStringLiteral("transmit/interval"), sendInterval());
    settings.setValue(QStringLiteral("transmit/checksum"), _checksumComboBox->currentData());
    settings.setValue(QStringLiteral("transmit/suffix"), _suffixComboBox->currentData());
    settings.setValue(QStringLiteral("layout/sidePanelPage"), _sidePanelStackedWidget->currentIndex());
    settings.remove(QStringLiteral("layout/sendMode"));
    settings.remove(QStringLiteral("quickSend/expanded"));
    if (_contentSplitter)
    {
        settings.setValue(QStringLiteral("layout/contentSplitterV2"), _contentSplitter->saveState());
    }
    settings.remove(QStringLiteral("layout/contentSplitter"));
    for (int index = 0; index < _quickSendItems.size(); ++index)
    {
        const QuickSendItem& item = _quickSendItems.at(index);
        const QString keyPrefix = QStringLiteral("quickSend/%1/").arg(index);
        settings.setValue(keyPrefix + QStringLiteral("mode"), item.modeComboBox->currentData());
        settings.setValue(keyPrefix + QStringLiteral("content"), item.contentEdit->text());
    }
    settings.sync();
}

/*****************************************************
函数名称：void MonitorPage::connectSettingsSignals()
入口参数：无
出口参数：无
函数功能：配置变化时立即保存，避免下次启动重复调整
*****************************************************/
void MonitorPage::connectSettingsSignals()
{
    connect(_portComboBox, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (index >= 0)
        {
            _preferredPortName = _portComboBox->currentData().toString();
            saveSettings();
        }
    });

    const QList<ElaComboBox*> comboBoxes = {
        _baudComboBox, _dataBitsComboBox, _parityComboBox, _stopBitsComboBox,
        _dataFormatComboBox, _checksumComboBox, _suffixComboBox};
    for (ElaComboBox* comboBox : comboBoxes)
    {
        connect(comboBox, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
            saveSettings();
        });
    }

    const QList<ElaToolButton*> displayButtons = {
        _timeButton, _utf8Button, _rxButton, _txButton, _ansiButton};
    for (ElaToolButton* button : displayButtons)
    {
        connect(button, &ElaToolButton::toggled, this, [this](bool) {
            saveSettings();
        });
    }

    connect(_inputModeButton, &ElaPushButton::toggled, this, [this](bool) {
        saveSettings();
    });
    connect(_frameTimeoutEdit, &ElaLineEdit::editingFinished,
            this, &MonitorPage::handleFrameTimeoutEditingFinished);
    connect(_trafficView, &TrafficTextEdit::textPointSizeChanged, this, [this](int) {
        saveSettings();
    });
    connect(_sendIntervalEdit, &ElaLineEdit::editingFinished,
            this, &MonitorPage::handleSendIntervalEditingFinished);

    for (const QuickSendItem& item : _quickSendItems)
    {
        connect(item.modeComboBox, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
            saveSettings();
        });
        connect(item.contentEdit, &ElaLineEdit::editingFinished, this, [this]() {
            saveSettings();
        });
    }
}

/*****************************************************
函数名称：void MonitorPage::updatePorts(const QStringList& portNames, const QStringList& descriptions)
入口参数：portNames为串口名列表，descriptions为设备描述列表
出口参数：无
函数功能：刷新串口选择框并保留原选择
*****************************************************/
void MonitorPage::updatePorts(const QStringList& portNames, const QStringList& descriptions)
{
    const bool previouslyHadPorts = _portComboBox->count() > 0;
    const QString currentPort = _portComboBox->currentData().toString();
    const QString targetPort = currentPort.isEmpty() ? _preferredPortName : currentPort;
    const QSignalBlocker blocker(_portComboBox);
    _portComboBox->clear();

    for (int index = 0; index < portNames.size(); ++index)
    {
        const QString description = index < descriptions.size() ? descriptions.at(index).trimmed() : QString();
        const QString displayText = description.isEmpty()
            ? portNames.at(index)
            : QStringLiteral("%1 - %2").arg(portNames.at(index), description);
        _portComboBox->addItem(displayText, portNames.at(index));
    }

    const int previousIndex = _portComboBox->findData(targetPort);
    if (previousIndex >= 0)
    {
        _portComboBox->setCurrentIndex(previousIndex);
    }

    if (_preferredPortName.isEmpty() && _portComboBox->count() > 0)
    {
        _preferredPortName = _portComboBox->currentData().toString();
        saveSettings();
    }

    _connectionSwitch->setEnabled(_connected || !portNames.isEmpty());
    if (!_connected)
    {
        _statusText->setText(portNames.isEmpty() ? QStringLiteral("未发现可用串口") : QStringLiteral("未连接"));
    }
    if (previouslyHadPorts && portNames.isEmpty())
    {
        ElaMessageBar::warning(ElaMessageBarType::BottomRight,
                               QStringLiteral("串口设备"),
                               QStringLiteral("未检测到可用串口，设备可能已拔出"),
                               3000,
                               this);
    }
}

/*****************************************************
函数名称：void MonitorPage::setConnectionState(bool connected, const QString& description)
入口参数：connected为连接状态，description为状态描述
出口参数：无
函数功能：同步连接开关、发送区和硬件参数状态
*****************************************************/
void MonitorPage::setConnectionState(bool connected, const QString& description)
{
    const bool wasConnected = _connected;
    _connected = connected;
    {
        const QSignalBlocker blocker(_connectionSwitch);
        _connectionSwitch->setIsToggled(connected);
    }

    setHardwareControlsEnabled(!connected);
    _connectionSwitch->setEnabled(connected || _portComboBox->count() > 0);
    _connectionText->setText(connected ? QStringLiteral("已连接") : QStringLiteral("连接"));
    _sendButton->setEnabled(connected);
    _continuousSendCheckBox->setEnabled(connected);
    if (!connected)
    {
        const QSignalBlocker blocker(_continuousSendCheckBox);
        _continuousSendCheckBox->setChecked(false);
        _continuousSendTimer->stop();
    }
    for (int index = 0; index < _quickSendItems.size(); ++index)
    {
        updateQuickSendItemState(index);
    }
    _statusText->setText(connected ? QStringLiteral("已连接：%1").arg(description) : description);
    if (connected && !wasConnected)
    {
        ElaMessageBar::success(ElaMessageBarType::BottomRight,
                               QStringLiteral("连接成功"),
                               description,
                               2500,
                               this);
    }
    else if (!connected && wasConnected)
    {
        ElaMessageBar::information(ElaMessageBarType::BottomRight,
                                   QStringLiteral("连接已断开"),
                                   description,
                                   2500,
                                   this);
    }
}

/*****************************************************
函数名称：void MonitorPage::appendReceivedData(const QByteArray& data)
入口参数：data为串口接收数据
出口参数：无
函数功能：累计并显示RX方向数据
*****************************************************/
void MonitorPage::appendReceivedData(const QByteArray& data)
{
    _rxBytes += static_cast<quint64>(data.size());
    appendTraffic(true, data);
}

/*****************************************************
函数名称：void MonitorPage::appendSentData(const QByteArray& data)
入口参数：data为串口发送数据
出口参数：无
函数功能：累计并显示TX方向数据
*****************************************************/
void MonitorPage::appendSentData(const QByteArray& data)
{
    _txBytes += static_cast<quint64>(data.size());
    appendTraffic(false, data);
}

/*****************************************************
函数名称：void MonitorPage::showError(const QString& message)
入口参数：message为错误描述
出口参数：无
函数功能：在页面底部显示错误信息并恢复错误连接开关
*****************************************************/
void MonitorPage::showError(const QString& message)
{
    ElaMessageBar::error(ElaMessageBarType::BottomRight,
                         QStringLiteral("操作失败"),
                         message,
                         3500,
                         this);
    if (!_connected && _connectionSwitch->getIsToggled())
    {
        const QSignalBlocker blocker(_connectionSwitch);
        _connectionSwitch->setIsToggled(false);
    }
}

/*****************************************************
函数名称：void MonitorPage::handleConnectionToggled(bool checked)
入口参数：checked为连接开关状态
出口参数：无
函数功能：根据连接开关请求打开或关闭串口
*****************************************************/
void MonitorPage::handleConnectionToggled(bool checked)
{
    if (!checked)
    {
        if (_connected)
        {
            emit closePortRequested();
        }
        return;
    }

    if (_portComboBox->count() == 0)
    {
        showError(QStringLiteral("没有可用串口"));
        return;
    }

    ElaMessageBar::information(ElaMessageBarType::BottomRight,
                               QStringLiteral("串口连接"),
                               QStringLiteral("正在连接 %1").arg(_portComboBox->currentData().toString()),
                               1800,
                               this);
    emit openPortRequested(_portComboBox->currentData().toString(),
                           _baudComboBox->currentText().toInt(),
                           _dataBitsComboBox->currentData().toInt(),
                           _parityComboBox->currentData().toInt(),
                           _stopBitsComboBox->currentData().toInt());
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
    const QByteArray data = parseTransmitData(valid);
    if (!valid)
    {
        _continuousSendTimer->stop();
        const QSignalBlocker blocker(_continuousSendCheckBox);
        _continuousSendCheckBox->setChecked(false);
        showError(QStringLiteral("HEX 数据必须由偶数个十六进制字符组成"));
        return;
    }

    if (data.isEmpty())
    {
        _continuousSendTimer->stop();
        const QSignalBlocker blocker(_continuousSendCheckBox);
        _continuousSendCheckBox->setChecked(false);
        showError(QStringLiteral("发送数据不能为空"));
        return;
    }

    emit sendDataRequested(data);
}

/*****************************************************
函数名称：void MonitorPage::handleTextTransmitChanged(const QString& text)
入口参数：text为文本发送框内容
出口参数：无
函数功能：将UTF-8文本实时转换为带空格的HEX内容
*****************************************************/
void MonitorPage::handleTextTransmitChanged(const QString& text)
{
    const QSignalBlocker blocker(_hexTransmitEdit);
    _hexTransmitEdit->setText(QString::fromLatin1(text.toUtf8().toHex(' ').toUpper()));
}

/*****************************************************
函数名称：void MonitorPage::formatHexLineEdit(ElaLineEdit* lineEdit) const
入口参数：lineEdit为需要按字节分组的HEX输入框
出口参数：无
函数功能：将合法HEX字符转换为大写并每两个字符插入一个空格，同时保持光标字节位置
*****************************************************/
void MonitorPage::formatHexLineEdit(ElaLineEdit* lineEdit) const
{
    const QString originalText = lineEdit->text();
    const int originalCursorPosition = lineEdit->cursorPosition();
    QString normalizedText;
    normalizedText.reserve(originalText.size());
    int hexCharactersBeforeCursor = 0;

    for (int index = 0; index < originalText.size(); ++index)
    {
        const QChar character = originalText.at(index);
        if (character.isSpace())
        {
            continue;
        }

        const QChar upperCharacter = character.toUpper();
        const bool isHexCharacter = (upperCharacter >= QLatin1Char('0') && upperCharacter <= QLatin1Char('9'))
            || (upperCharacter >= QLatin1Char('A') && upperCharacter <= QLatin1Char('F'));
        if (!isHexCharacter)
        {
            return;
        }

        normalizedText.append(upperCharacter);
        if (index < originalCursorPosition)
        {
            ++hexCharactersBeforeCursor;
        }
    }

    QString formattedText;
    formattedText.reserve(normalizedText.size() + normalizedText.size() / 2);
    for (int index = 0; index < normalizedText.size(); ++index)
    {
        if (index > 0 && index % 2 == 0)
        {
            formattedText.append(QLatin1Char(' '));
        }
        formattedText.append(normalizedText.at(index));
    }

    const int formattedCursorPosition = qMin(
        hexCharactersBeforeCursor + hexCharactersBeforeCursor / 2,
        formattedText.size());
    if (formattedText == originalText && lineEdit->cursorPosition() == formattedCursorPosition)
    {
        return;
    }

    const QSignalBlocker blocker(lineEdit);
    lineEdit->setText(formattedText);
    lineEdit->setCursorPosition(formattedCursorPosition);
}

/*****************************************************
函数名称：void MonitorPage::handleHexTransmitChanged(const QString& text)
入口参数：text为HEX发送框内容
出口参数：无
函数功能：HEX内容完整合法时实时转换为UTF-8文本
*****************************************************/
void MonitorPage::handleHexTransmitChanged(const QString& text)
{
    Q_UNUSED(text);
    formatHexLineEdit(_hexTransmitEdit);

    QString normalized = _hexTransmitEdit->text();
    normalized.remove(QRegularExpression(QStringLiteral("\\s+")));

    const QRegularExpression hexPattern(QStringLiteral("^[0-9A-Fa-f]*$"));
    const bool valid = (normalized.size() % 2 == 0) && hexPattern.match(normalized).hasMatch();
    if (!valid)
    {
        return;
    }

    const QSignalBlocker blocker(_textTransmitEdit);
    _textTransmitEdit->setText(QString::fromUtf8(QByteArray::fromHex(normalized.toLatin1())));
}

/*****************************************************
函数名称：void MonitorPage::handleFrameTimeoutEditingFinished()
入口参数：无
出口参数：无
函数功能：规范断帧文本输入并同步到串口控制器和本地配置
*****************************************************/
void MonitorPage::handleFrameTimeoutEditingFinished()
{
    const int milliseconds = frameTimeout();
    {
        const QSignalBlocker blocker(_frameTimeoutEdit);
        _frameTimeoutEdit->setText(QString::number(milliseconds));
    }
    saveSettings();
    emit frameTimeoutChanged(milliseconds);
}

/*****************************************************
函数名称：void MonitorPage::handleSidePanelPageChanged(int index)
入口参数：index为右侧页面序号，0为单词发送，1为多字符串发送，2为报文搜索
出口参数：无
函数功能：使用ElaPivot切换右侧共享空间显示的发送或报文搜索页面并保存选择
*****************************************************/
void MonitorPage::handleSidePanelPageChanged(int index)
{
    if (index < 0 || index >= _sidePanelStackedWidget->count())
    {
        return;
    }

    _sidePanelStackedWidget->setCurrentIndex(index);
    saveSettings();
}

/*****************************************************
函数名称：void MonitorPage::handleContinuousSendToggled(bool checked)
入口参数：checked为连续发送是否启用
出口参数：无
函数功能：按配置间隔启动或停止主发送区周期发送
*****************************************************/
void MonitorPage::handleContinuousSendToggled(bool checked)
{
    if (!checked)
    {
        _continuousSendTimer->stop();
        ElaMessageBar::information(ElaMessageBarType::BottomRight,
                                   QStringLiteral("连续发送"),
                                   QStringLiteral("连续发送已停止"),
                                   1800,
                                   this);
        return;
    }

    if (!_connected)
    {
        const QSignalBlocker blocker(_continuousSendCheckBox);
        _continuousSendCheckBox->setChecked(false);
        showError(QStringLiteral("串口未连接，无法连续发送"));
        return;
    }

    bool valid = false;
    const QByteArray data = parseTransmitData(valid);
    if (!valid || data.isEmpty())
    {
        const QSignalBlocker blocker(_continuousSendCheckBox);
        _continuousSendCheckBox->setChecked(false);
        showError(valid ? QStringLiteral("发送数据不能为空")
                        : QStringLiteral("HEX 数据必须由偶数个十六进制字符组成"));
        return;
    }

    _continuousSendTimer->start(sendInterval());
    emit sendDataRequested(data);
    ElaMessageBar::success(ElaMessageBarType::BottomRight,
                           QStringLiteral("连续发送"),
                           QStringLiteral("已启动，发送间隔 %1 ms").arg(sendInterval()),
                           2200,
                           this);
}

/*****************************************************
函数名称：void MonitorPage::handleSendIntervalEditingFinished()
入口参数：无
出口参数：无
函数功能：规范连续发送间隔并立即更新运行中的定时器
*****************************************************/
void MonitorPage::handleSendIntervalEditingFinished()
{
    const int milliseconds = sendInterval();
    {
        const QSignalBlocker blocker(_sendIntervalEdit);
        _sendIntervalEdit->setText(QString::number(milliseconds));
    }
    saveSettings();
    if (_continuousSendTimer->isActive())
    {
        _continuousSendTimer->start(milliseconds);
    }
}

/*****************************************************
函数名称：void MonitorPage::clearTraffic()
入口参数：无
出口参数：无
函数功能：清空显示记录和累计收发统计
*****************************************************/
void MonitorPage::clearTraffic()
{
    _trafficRecords.clear();
    _trafficSearchResultsModel->clear();
    _nextTrafficSearchBlockId = 0;
    _trafficView->clear();
    _rxBytes = 0;
    _txBytes = 0;
    _lastRxBytes = 0;
    _lastTxBytes = 0;
    updateTransferRate();
    updateTrafficSearch();
}

/*****************************************************
函数名称：void MonitorPage::renderTraffic()
入口参数：无
出口参数：无
函数功能：依据当前工具栏选项重新绘制历史通讯记录
*****************************************************/
void MonitorPage::renderTraffic()
{
    QScrollBar* scrollBar = _trafficView->verticalScrollBar();
    const int lockedScrollPosition = scrollBar->value();
    const bool searchActive = !_trafficSearchQuery.isEmpty();
    _nextTrafficSearchBlockId = 0;
    _trafficView->setUpdatesEnabled(false);
    _trafficView->clear();
    for (const TrafficRecord& record : _trafficRecords)
    {
        if ((record.received && !_rxButton->isChecked()) || (!record.received && !_txButton->isChecked()))
        {
            continue;
        }
        appendFormattedTraffic(record);
    }
    _trafficView->setUpdatesEnabled(true);
    scrollBar->setValue((_pauseScrollButton->isChecked() || searchActive)
        ? qMin(lockedScrollPosition, scrollBar->maximum())
        : scrollBar->maximum());
    updateTrafficSearch();
}

/*****************************************************
函数名称：void MonitorPage::handleLogToggled(bool checked)
入口参数：checked为日志记录开关状态
出口参数：无
函数功能：由用户选择保存目录后打开或关闭通讯日志文件
*****************************************************/
void MonitorPage::handleLogToggled(bool checked)
{
    if (!checked)
    {
        if (_logFile && _logFile->isOpen())
        {
            _logFile->close();
        }
        ElaMessageBar::information(ElaMessageBarType::BottomRight,
                                   QStringLiteral("日志记录"),
                                   QStringLiteral("日志记录已停止"),
                                   1800,
                                   this);
        return;
    }

    QSettings settings(QStringLiteral("CommunicationAnalyzer"), QStringLiteral("CommunicationAnalyzer"));
    const QString previousDirectory = settings.value(QStringLiteral("log/directory"), QDir::homePath()).toString();
    const QString directoryPath = QFileDialog::getExistingDirectory(
        this,
        QStringLiteral("选择日志保存目录"),
        previousDirectory,
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (directoryPath.isEmpty())
    {
        const QSignalBlocker blocker(_logButton);
        _logButton->setChecked(false);
        return;
    }

    if (!_logFile)
    {
        _logFile = new QFile(this);
    }
    _logFile->setFileName(QDir(directoryPath).filePath(QStringLiteral("traffic.log")));

    if (!_logFile->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
    {
        const QSignalBlocker blocker(_logButton);
        _logButton->setChecked(false);
        showError(QStringLiteral("无法打开日志文件：%1").arg(_logFile->errorString()));
        return;
    }

    settings.setValue(QStringLiteral("log/directory"), directoryPath);
    ElaMessageBar::success(ElaMessageBarType::BottomRight,
                           QStringLiteral("日志记录"),
                           QStringLiteral("日志已保存到：%1").arg(QDir::toNativeSeparators(_logFile->fileName())),
                           3000,
                           this);
}

/*****************************************************
函数名称：void MonitorPage::handlePauseScrollToggled(bool paused)
入口参数：paused为是否暂停接收窗口自动滚动
出口参数：无
函数功能：锁定或恢复接收窗口滚动位置，数据始终继续追加
*****************************************************/
void MonitorPage::handlePauseScrollToggled(bool paused)
{
    _pauseScrollButton->setText(paused ? QStringLiteral("恢复滚动") : QStringLiteral("暂停滚动"));
    _pauseScrollButton->setElaIcon(paused ? ElaIconType::Play : ElaIconType::Pause);

    if (paused)
    {
        ElaMessageBar::information(ElaMessageBarType::BottomRight,
                                   QStringLiteral("接收窗口"),
                                   QStringLiteral("自动滚动已暂停，数据仍在实时追加"),
                                   2200,
                                   this);
        return;
    }

    _trafficView->verticalScrollBar()->setValue(_trafficView->verticalScrollBar()->maximum());
    ElaMessageBar::success(ElaMessageBarType::BottomRight,
                           QStringLiteral("接收窗口"),
                           QStringLiteral("自动滚动已恢复"),
                           1800,
                           this);
}

/*****************************************************
函数名称：void MonitorPage::updateTransferRate()
入口参数：无
出口参数：无
函数功能：计算并显示最近一秒收发字节率和累计字节数
*****************************************************/
void MonitorPage::updateTransferRate()
{
    const quint64 rxRate = _rxBytes - _lastRxBytes;
    const quint64 txRate = _txBytes - _lastTxBytes;
    _lastRxBytes = _rxBytes;
    _lastTxBytes = _txBytes;
    _transferText->setText(QStringLiteral("↓ %1 B/s  %2 B    ↑ %3 B/s  %4 B")
                               .arg(rxRate)
                               .arg(_rxBytes)
                               .arg(txRate)
                                .arg(_txBytes));
}

/*****************************************************
函数名称：void MonitorPage::updateTrafficSearch()
入口参数：无
出口参数：无
函数功能：高亮接收窗口内的全部命中内容并将命中报文更新到Ela列表
*****************************************************/
void MonitorPage::updateTrafficSearch()
{
    const QModelIndex previousIndex = _trafficSearchResults->currentIndex();
    const int previousBlockId = previousIndex.isValid()
        ? previousIndex.data(Qt::UserRole).toInt()
        : -1;

    QList<QTextBlock> matchingBlocks;
    QSet<int> matchingBlockIds;
    QList<QTextEdit::ExtraSelection> highlightedSelections;
    if (!_trafficSearchQuery.isEmpty())
    {
        QTextCursor searchCursor(_trafficView->document());
        while (true)
        {
            const QTextCursor matchCursor = _trafficView->document()->find(_trafficSearchQuery, searchCursor);
            if (matchCursor.isNull())
            {
                break;
            }

            searchCursor = matchCursor;

            const QTextBlock block = matchCursor.block();
            if (block.isValid() && block.userState() >= 0
                && !matchingBlockIds.contains(block.userState()))
            {
                matchingBlockIds.insert(block.userState());
                matchingBlocks.append(block);
            }

            QTextEdit::ExtraSelection selection;
            selection.cursor = matchCursor;
            selection.format.setBackground(QColor(QStringLiteral("#FFF2A8")));
            highlightedSelections.append(selection);
        }
    }

    _trafficView->setExtraSelections(highlightedSelections);
    _trafficSearchResultsModel->clear();

    int selectedRow = -1;
    for (const QTextBlock& block : matchingBlocks)
    {
        const QString resultText = block.text().simplified();
        if (resultText.isEmpty())
        {
            continue;
        }

        auto* resultItem = new QStandardItem(resultText);
        resultItem->setEditable(false);
        resultItem->setToolTip(block.text());
        resultItem->setData(block.userState(), Qt::UserRole);
        _trafficSearchResultsModel->appendRow(resultItem);
        if (block.userState() == previousBlockId)
        {
            selectedRow = _trafficSearchResultsModel->rowCount() - 1;
        }
    }

    if (selectedRow >= 0)
    {
        _trafficSearchResults->setCurrentIndex(
            _trafficSearchResultsModel->index(selectedRow, 0));
    }
}

/*****************************************************
函数名称：void MonitorPage::locateTrafficSearchResult(int blockId)
入口参数：blockId为搜索结果对应的接收文档块标识
出口参数：无
函数功能：按稳定文档块标识查找报文并将接收窗口滚动到该报文位置
*****************************************************/
void MonitorPage::locateTrafficSearchResult(int blockId)
{
    if (blockId < 0)
    {
        return;
    }

    for (QTextBlock block = _trafficView->document()->begin(); block.isValid(); block = block.next())
    {
        if (block.userState() == blockId)
        {
            QTextCursor cursor(block);
            const int matchOffset = block.text().indexOf(
                _trafficSearchQuery, 0, Qt::CaseInsensitive);
            if (matchOffset >= 0)
            {
                cursor.setPosition(block.position() + matchOffset);
            }
            _trafficView->setTextCursor(cursor);
            _trafficView->centerCursor();
            return;
        }
    }
}

/*****************************************************
函数名称：QByteArray MonitorPage::parseTransmitData(bool& valid) const
入口参数：valid返回输入是否合法
出口参数：完成校验和结束符处理后的字节数据
函数功能：按照文本或HEX输入模式解析发送内容
*****************************************************/
QByteArray MonitorPage::parseTransmitData(bool& valid) const
{
    const bool hexMode = _inputModeButton->isChecked();
    const QString input = hexMode ? _hexTransmitEdit->text() : _textTransmitEdit->text();
    QByteArray data = parseTransmitInput(input, hexMode, valid);
    if (!valid)
    {
        return QByteArray();
    }

    appendChecksum(data);
    data.append(_suffixComboBox->currentData().toByteArray());
    return data;
}

/*****************************************************
函数名称：QByteArray MonitorPage::parseTransmitInput(const QString& input, bool hexMode, bool& valid) const
入口参数：input为输入内容，hexMode为HEX解析标记，valid返回输入是否合法
出口参数：解析后的原始字节数据
函数功能：统一解析主发送区和快捷发送区的文本或HEX内容
*****************************************************/
QByteArray MonitorPage::parseTransmitInput(const QString& input, bool hexMode, bool& valid) const
{
    if (!hexMode)
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
函数名称：void MonitorPage::sendQuickItem(int index)
入口参数：index为快捷发送配置序号
出口参数：无
函数功能：解析并发送指定多字符串预设内容
*****************************************************/
void MonitorPage::sendQuickItem(int index)
{
    if (index < 0 || index >= _quickSendItems.size())
    {
        return;
    }

    const QuickSendItem& item = _quickSendItems.at(index);
    bool valid = false;
    QByteArray data = parseTransmitInput(item.contentEdit->text(),
                                         item.modeComboBox->currentData().toInt() == 1,
                                         valid);
    if (!valid)
    {
        showError(QStringLiteral("预设 %1 的HEX数据格式不正确").arg(index + 1));
        return;
    }

    appendChecksum(data);
    data.append(_suffixComboBox->currentData().toByteArray());
    if (data.isEmpty())
    {
        showError(QStringLiteral("预设 %1 的发送数据不能为空").arg(index + 1));
        return;
    }

    emit sendDataRequested(data);
}

/*****************************************************
函数名称：void MonitorPage::updateQuickSendItemState(int index)
入口参数：index为快捷发送配置序号
出口参数：无
函数功能：实时检测HEX格式并根据连接和输入状态控制发送按钮
*****************************************************/
void MonitorPage::updateQuickSendItemState(int index)
{
    if (index < 0 || index >= _quickSendItems.size())
    {
        return;
    }

    QuickSendItem& item = _quickSendItems[index];
    const bool hexMode = item.modeComboBox->currentData().toInt() == 1;
    item.contentEdit->setValidator(hexMode ? item.hexValidator : nullptr);
    if (hexMode)
    {
        formatHexLineEdit(item.contentEdit);
    }

    bool valid = !item.contentEdit->text().isEmpty();
    if (valid && hexMode)
    {
        bool hexValid = false;
        const QByteArray data = parseTransmitInput(item.contentEdit->text(), true, hexValid);
        valid = hexValid && !data.isEmpty();
    }

    item.sendButton->setEnabled(_connected && valid);
    if (hexMode && !item.contentEdit->text().isEmpty() && !valid)
    {
        item.contentEdit->setToolTip(QStringLiteral("HEX必须由偶数个十六进制字符组成"));
        item.sendButton->setToolTip(QStringLiteral("HEX格式不完整，无法发送"));
    }
    else
    {
        item.contentEdit->setToolTip(hexMode
            ? QStringLiteral("HEX格式有效后可发送")
            : QStringLiteral("输入文本字符串"));
        item.sendButton->setToolTip(QString());
    }
}

/*****************************************************
函数名称：void MonitorPage::appendChecksum(QByteArray& data) const
入口参数：data为待追加校验的发送数据
出口参数：无
函数功能：计算并追加XOR8、SUM8或Modbus CRC16校验
*****************************************************/
void MonitorPage::appendChecksum(QByteArray& data) const
{
    const int algorithm = _checksumComboBox->currentData().toInt();
    if (algorithm == 0)
    {
        return;
    }

    if (algorithm == 1 || algorithm == 2)
    {
        quint8 checksum = 0;
        for (const char byte : data)
        {
            checksum = algorithm == 1
                ? static_cast<quint8>(checksum ^ static_cast<quint8>(byte))
                : static_cast<quint8>(checksum + static_cast<quint8>(byte));
        }
        data.append(static_cast<char>(checksum));
        return;
    }

    quint16 crc = 0xFFFF;
    for (const char byte : data)
    {
        crc ^= static_cast<quint8>(byte);
        for (int bit = 0; bit < 8; ++bit)
        {
            crc = (crc & 0x0001) ? static_cast<quint16>((crc >> 1) ^ 0xA001) : static_cast<quint16>(crc >> 1);
        }
    }
    data.append(static_cast<char>(crc & 0xFF));
    data.append(static_cast<char>((crc >> 8) & 0xFF));
}

/*****************************************************
函数名称：void MonitorPage::appendTraffic(bool received, const QByteArray& data)
入口参数：received为接收方向标记，data为原始字节数据
出口参数：无
函数功能：保存、显示并按需记录一条通讯数据
*****************************************************/
void MonitorPage::appendTraffic(bool received, const QByteArray& data)
{
    const TrafficRecord record{received, QDateTime::currentDateTime(), data};
    _trafficRecords.append(record);
    if (_trafficRecords.size() > 10000)
    {
        _trafficRecords.removeFirst();
    }

    const bool displayReceived = received && _rxButton->isChecked();
    const bool displaySent = !received && _txButton->isChecked();
    if (displayReceived || displaySent)
    {
        QScrollBar* scrollBar = _trafficView->verticalScrollBar();
        const int lockedScrollPosition = scrollBar->value();
        const bool searchActive = !_trafficSearchQuery.isEmpty();
        appendFormattedTraffic(record);
        scrollBar->setValue((_pauseScrollButton->isChecked() || searchActive)
            ? qMin(lockedScrollPosition, scrollBar->maximum())
            : scrollBar->maximum());
        if (searchActive)
        {
            _trafficSearchTimer->start();
        }
    }

    if (_logButton->isChecked())
    {
        writeLog(record);
    }
}

/*****************************************************
函数名称：void MonitorPage::appendFormattedTraffic(const TrafficRecord& record)
入口参数：record为原始通讯记录
出口参数：无
函数功能：按普通、HEX或HEX对照模式插入一帧通讯数据
*****************************************************/
void MonitorPage::appendFormattedTraffic(const TrafficRecord& record)
{
    QString textContent = _utf8Button->isChecked()
        ? QString::fromUtf8(record.data)
        : QString::fromLatin1(record.data);
    const QString hexContent = QString::fromLatin1(record.data.toHex(' ').toUpper());

    if (_ansiButton->isChecked())
    {
        textContent = stripAnsiSequences(textContent);
    }
    else
    {
        textContent.replace(QChar(0x1B), QStringLiteral("\\x1B"));
    }
    // Windows文本剪贴板以空字符结尾，直接保留0x00会导致全选复制在此处截断。
    textContent.replace(QChar::Null, QStringLiteral("\\x00"));

    const QString timestamp = record.timestamp.toString(QStringLiteral("HH:mm:ss.zzz"));

    QTextCharFormat defaultFormat;
    defaultFormat.setForeground(_trafficView->palette().color(QPalette::Text));

    QTextCharFormat timeFormat;
    timeFormat.setForeground(Qt::black);
    timeFormat.setFontWeight(QFont::DemiBold);

    QTextCharFormat directionFormat;
    directionFormat.setForeground(record.received
        ? QColor(QStringLiteral("#16A34A"))
        : QColor(QStringLiteral("#2563EB")));
    directionFormat.setFontWeight(QFont::Bold);

    QTextCharFormat dataFormat;
    dataFormat.setForeground(Qt::black);

    QTextCharFormat hexFormat;
    hexFormat.setForeground(QColor(QStringLiteral("#006400")));
    hexFormat.setFontWeight(QFont::DemiBold);

    QTextCursor cursor = _trafficView->textCursor();
    cursor.movePosition(QTextCursor::End);
    if (!_trafficView->document()->isEmpty())
    {
        cursor.setCharFormat(defaultFormat);
        cursor.insertBlock();
    }
    const QTextBlock firstRecordBlock = cursor.block();

    if (_timeButton->isChecked())
    {
        cursor.insertText(QStringLiteral("[%1]").arg(timestamp),
                          timeFormat);
        cursor.insertText(QStringLiteral(" "), defaultFormat);
    }

    cursor.insertText(record.received ? QStringLiteral("[RX]") : QStringLiteral("[TX]"), directionFormat);
    cursor.insertText(QStringLiteral(" "), defaultFormat);

    const int displayMode = _dataFormatComboBox->currentData().toInt();
    if (displayMode == 2)
    {
        cursor.insertText(textContent, dataFormat);
        if (!textContent.endsWith(QChar('\n')))
        {
            cursor.insertText(QStringLiteral("\n"), dataFormat);
        }
        cursor.insertText(hexContent, hexFormat);
    }
    else if (displayMode == 1)
    {
        cursor.insertText(hexContent, hexFormat);
    }
    else
    {
        cursor.insertText(textContent, dataFormat);
    }

    cursor.setCharFormat(defaultFormat);
    _trafficView->setTextCursor(cursor);

    const QTextBlock lastRecordBlock = cursor.block();
    for (QTextBlock block = firstRecordBlock; block.isValid(); block = block.next())
    {
        block.setUserState(_nextTrafficSearchBlockId++);
        if (block == lastRecordBlock)
        {
            break;
        }
    }
}

/*****************************************************
函数名称：QString MonitorPage::stripAnsiSequences(const QString& text) const
入口参数：text为可能带ANSI转义序列的文本
出口参数：移除控制序列后的文本
函数功能：清除常见ANSI终端颜色和光标控制序列
*****************************************************/
QString MonitorPage::stripAnsiSequences(const QString& text) const
{
    QString result = text;
    static const QRegularExpression ansiPattern(QStringLiteral("\\x1B(?:[@-Z\\\\-_]|\\[[0-?]*[ -/]*[@-~])"));
    result.remove(ansiPattern);
    return result;
}

/*****************************************************
函数名称：void MonitorPage::writeLog(const TrafficRecord& record)
入口参数：record为待记录通讯数据
出口参数：无
函数功能：将时间、方向和HEX字节写入日志文件
*****************************************************/
void MonitorPage::writeLog(const TrafficRecord& record)
{
    if (!_logFile || !_logFile->isOpen())
    {
        return;
    }

    QTextStream stream(_logFile);
    stream << record.timestamp.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"))
           << ' ' << (record.received ? QStringLiteral("RX") : QStringLiteral("TX"))
           << "  " << QString::fromLatin1(record.data.toHex(' ').toUpper()) << '\n';
    stream.flush();
}

/*****************************************************
函数名称：void MonitorPage::setHardwareControlsEnabled(bool enabled)
入口参数：enabled为硬件参数是否允许编辑
出口参数：无
函数功能：连接后锁定硬件参数，断开后恢复编辑
*****************************************************/
void MonitorPage::setHardwareControlsEnabled(bool enabled)
{
    _transportComboBox->setEnabled(enabled);
    _portComboBox->setEnabled(enabled);
    _baudComboBox->setEnabled(enabled);
    _dataBitsComboBox->setEnabled(enabled);
    _parityComboBox->setEnabled(enabled);
    _stopBitsComboBox->setEnabled(enabled);
    _refreshButton->setEnabled(enabled);
}
