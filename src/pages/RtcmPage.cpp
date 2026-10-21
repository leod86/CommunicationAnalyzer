#include "pages/RtcmPage.h"

#include <QColor>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHash>
#include <QItemSelectionModel>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QStringList>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

#include "ElaDef.h"
#include "ElaPivot.h"
#include "ElaTableView.h"
#include "ElaText.h"
#include "ElaToolButton.h"
#include "ElaTreeView.h"

namespace
{
// 帧列表列号。
enum FrameColumn
{
    SequenceColumn = 0,  // 帧序号
    TimeColumn,          // 到达时间
    MessageColumn,       // 消息号
    NameColumn,          // 消息名称
    LengthColumn,        // 载荷长度
    CrcColumn,           // CRC 校验结果
    SummaryColumn,       // 摘要
    FrameColumnCount     // 列数
};

// 帧列表最多保留的帧数。
constexpr int kMaximumFrameRows = 5000;
// 帧批量插入的刷新间隔。
constexpr int kFlushIntervalMilliseconds = 100;
// 原始数据每行显示的字节数。
constexpr int kRawBytesPerLine = 16;
}  // namespace

/*****************************************************
函数名称：RtcmPage::RtcmPage(QWidget* parent)
入口参数：parent为父窗口
出口参数：无
函数功能：创建RTCM3解析页面
*****************************************************/
RtcmPage::RtcmPage(QWidget* parent)
    : ElaScrollPage(parent)
{
    buildUi();
}

/*****************************************************
函数名称：RtcmPage::~RtcmPage()
入口参数：无
出口参数：无
函数功能：释放RTCM3解析页面
*****************************************************/
RtcmPage::~RtcmPage() = default;

/*****************************************************
函数名称：void RtcmPage::buildUi()
入口参数：无
出口参数：无
函数功能：构建帧列表与详情面板布局
*****************************************************/
void RtcmPage::buildUi()
{
    setTitleVisible(false);
    setWindowTitle(QStringLiteral("RTCM3 解析"));

    auto* pageContent = new QWidget(this);
    auto* pageLayout = new QVBoxLayout(pageContent);
    pageLayout->setContentsMargins(8, 8, 8, 8);
    pageLayout->setSpacing(8);

    // 顶部状态与操作行。
    auto* headerLayout = new QHBoxLayout();
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(8);
    _statusText = new ElaText(QStringLiteral("串口未连接"), 13, pageContent);
    _statusText->setWordWrap(true);
    headerLayout->addWidget(_statusText, 1);
    _clearButton = new ElaToolButton(pageContent);
    _clearButton->setText(QStringLiteral("清空"));
    _clearButton->setElaIcon(ElaIconType::TrashCanList);
    _clearButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    _clearButton->setFixedHeight(30);
    _clearButton->setToolTip(QStringLiteral("清空已解析的帧与统计信息"));
    headerLayout->addWidget(_clearButton);
    pageLayout->addLayout(headerLayout);

    // 帧列表。
    _frameModel = new QStandardItemModel(this);
    _frameModel->setHorizontalHeaderLabels({QStringLiteral("序号"),
                                            QStringLiteral("时间"),
                                            QStringLiteral("消息"),
                                            QStringLiteral("名称"),
                                            QStringLiteral("长度"),
                                            QStringLiteral("CRC"),
                                            QStringLiteral("摘要")});
    _frameTable = new ElaTableView(pageContent);
    _frameTable->setModel(_frameModel);
    _frameTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    _frameTable->setSelectionMode(QAbstractItemView::SingleSelection);
    _frameTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    _frameTable->setAlternatingRowColors(true);
    _frameTable->setWordWrap(false);
    _frameTable->verticalHeader()->setVisible(false);
    _frameTable->verticalHeader()->setDefaultSectionSize(26);
    _frameTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    _frameTable->horizontalHeader()->setSectionResizeMode(SummaryColumn, QHeaderView::Stretch);
    _frameTable->setColumnWidth(SequenceColumn, 56);
    _frameTable->setColumnWidth(TimeColumn, 88);
    _frameTable->setColumnWidth(MessageColumn, 70);
    _frameTable->setColumnWidth(NameColumn, 128);
    _frameTable->setColumnWidth(LengthColumn, 50);
    _frameTable->setColumnWidth(CrcColumn, 50);
    _frameTable->setMinimumWidth(440);

    auto* splitter = new QSplitter(Qt::Horizontal, pageContent);
    splitter->setChildrenCollapsible(false);
    splitter->setHandleWidth(8);
    splitter->addWidget(_frameTable);
    splitter->addWidget(buildDetailPanel());
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 0);
    splitter->setSizes({640, 340});
    pageLayout->addWidget(splitter, 1);

    addCentralWidget(pageContent, true, false, 0);

    // 批量插入定时器与信号连接。
    _flushTimer = new QTimer(this);
    _flushTimer->setSingleShot(true);
    _flushTimer->setInterval(kFlushIntervalMilliseconds);
    connect(_flushTimer, &QTimer::timeout, this, &RtcmPage::flushPendingFrames);
    connect(_clearButton, &ElaToolButton::clicked, this, &RtcmPage::clearFrames);
    connect(_frameTable->selectionModel(), &QItemSelectionModel::currentRowChanged,
            this, &RtcmPage::handleCurrentFrameChanged);

    refreshStatusText();
    refreshStatistics();
    // 启动时先给出未选中提示，避免详情区空白。
    showFrameDetail(-1);
}

/*****************************************************
函数名称：QWidget* RtcmPage::buildDetailPanel()
入口参数：无
出口参数：详情面板控件
函数功能：构建帧详情与统计页签
*****************************************************/
QWidget* RtcmPage::buildDetailPanel()
{
    auto* panel = new QFrame(this);
    panel->setFrameShape(QFrame::StyledPanel);
    panel->setMinimumWidth(280);
    auto* panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(8, 7, 8, 8);
    panelLayout->setSpacing(7);

    auto* pivot = new ElaPivot(panel);
    pivot->setFixedHeight(34);
    pivot->setTextPixelSize(13);
    pivot->setPivotSpacing(6);
    pivot->setMarkWidth(52);
    pivot->appendPivot(QStringLiteral("帧详情"));
    pivot->appendPivot(QStringLiteral("统计"));

    auto* stackedWidget = new QStackedWidget(panel);

    // 帧详情页：按分组展示解码字段。
    _detailModel = new QStandardItemModel(this);
    _detailModel->setHorizontalHeaderLabels({QStringLiteral("字段"), QStringLiteral("值")});
    _detailTree = new ElaTreeView(stackedWidget);
    _detailTree->setModel(_detailModel);
    _detailTree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    _detailTree->setAlternatingRowColors(true);
    _detailTree->setUniformRowHeights(true);
    _detailTree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    _detailTree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    stackedWidget->addWidget(_detailTree);

    // 统计页：消息计数与帧率。
    _statisticsText = new ElaText(QStringLiteral("暂无统计数据"), 13, stackedWidget);
    _statisticsText->setWordWrap(true);
    _statisticsText->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    _statisticsText->setTextInteractionFlags(Qt::TextSelectableByMouse);
    stackedWidget->addWidget(_statisticsText);

    panelLayout->addWidget(pivot);
    panelLayout->addWidget(stackedWidget, 1);

    // 页签与堆叠页面一一对应。
    connect(pivot, &ElaPivot::pivotClicked, stackedWidget, &QStackedWidget::setCurrentIndex);
    return panel;
}

/*****************************************************
函数名称：void RtcmPage::appendRawData(const QByteArray& data)
入口参数：data为串口新到达的原始字节
出口参数：无
函数功能：将原始字节送入解析器并把解析结果排队等待插入
*****************************************************/
void RtcmPage::appendRawData(const QByteArray& data)
{
    const QList<Rtcm3Frame> frames = _parser.append(data);
    if (!frames.isEmpty())
    {
        _pendingFrames.append(frames);
    }
    // 即使本批没有解析出完整帧，也要触发刷新，保证噪声字节与CRC计数和状态栏同步。
    if (!_flushTimer->isActive())
    {
        _flushTimer->start();
    }
}

/*****************************************************
函数名称：void RtcmPage::setConnectionState(bool connected, const QString& description)
入口参数：connected为连接状态；description为串口描述
出口参数：无
函数功能：更新串口连接状态并刷新顶部提示
*****************************************************/
void RtcmPage::setConnectionState(bool connected, const QString& description)
{
    _connected = connected;
    _connectionDescription = description;
    refreshStatusText();
}

/*****************************************************
函数名称：void RtcmPage::clearFrames()
入口参数：无
出口参数：无
函数功能：清空已解析帧、缓存与统计信息
*****************************************************/
void RtcmPage::clearFrames()
{
    _parser.reset();
    _pendingFrames.clear();
    _frames.clear();
    _frameModel->removeRows(0, _frameModel->rowCount());
    _selectedFrameIndex = -1;
    _frameSequence = 0;
    _firstFrameTime = QDateTime();
    _autoFollow = true;
    // 清空后恢复详情面板的未选中提示。
    showFrameDetail(-1);
    refreshStatistics();
    refreshStatusText();
}

/*****************************************************
函数名称：void RtcmPage::flushPendingFrames()
入口参数：无
出口参数：无
函数功能：将排队的帧批量插入帧列表并维护缓存上限
*****************************************************/
void RtcmPage::flushPendingFrames()
{
    if (!_pendingFrames.isEmpty())
    {
        for (const Rtcm3Frame& frame : _pendingFrames)
        {
            insertFrameRow(frame);
            _frames.append(frame);
            if (!_firstFrameTime.isValid())
            {
                _firstFrameTime = frame.timestamp;
            }
        }
        _pendingFrames.clear();

        // 超出上限时从头部裁剪，缓存与表格保持一一对应。
        const int excess = _frames.size() - kMaximumFrameRows;
        if (excess > 0)
        {
            _frameModel->removeRows(0, excess);
            _frames.erase(_frames.begin(), _frames.begin() + excess);
            if (_selectedFrameIndex >= 0)
            {
                _selectedFrameIndex -= excess;
                if (_selectedFrameIndex < 0)
                {
                    _selectedFrameIndex = -1;
                    // 选中帧已被裁剪掉，恢复未选中提示。
                    showFrameDetail(-1);
                }
            }
        }

        if (_autoFollow && _frameModel->rowCount() > 0)
        {
            // 自动跟随时选中最新帧，使详情面板同步显示最新解码结果。
            _frameTable->setCurrentIndex(_frameModel->index(_frameModel->rowCount() - 1, 0));
            _frameTable->scrollToBottom();
        }
    }

    // 本批可能没有完整帧，但仍需刷新计数，避免统计页签落后于顶部状态栏。
    refreshStatistics();
    refreshStatusText();
}

/*****************************************************
函数名称：void RtcmPage::insertFrameRow(const Rtcm3Frame& frame)
入口参数：frame为待插入的帧
出口参数：无
函数功能：向帧列表追加一行显示数据
*****************************************************/
void RtcmPage::insertFrameRow(const Rtcm3Frame& frame)
{
    ++_frameSequence;

    auto* sequenceItem = new QStandardItem(QString::number(_frameSequence));
    sequenceItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);

    auto* timeItem = new QStandardItem(frame.timestamp.toString(QStringLiteral("HH:mm:ss.zzz")));
    timeItem->setTextAlignment(Qt::AlignHCenter | Qt::AlignVCenter);

    auto* messageItem = new QStandardItem(QString::number(frame.messageNumber));
    messageItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);

    auto* nameItem = new QStandardItem(frame.messageName);

    auto* lengthItem = new QStandardItem(QString::number(frame.payloadLength));
    lengthItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);

    auto* crcItem = new QStandardItem(frame.crcValid ? QStringLiteral("通过") : QStringLiteral("错误"));
    crcItem->setTextAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
    if (!frame.crcValid)
    {
        crcItem->setForeground(QColor(0xD9, 0x3B, 0x3B));
    }

    auto* summaryItem = new QStandardItem(frame.summary);

    _frameModel->appendRow({sequenceItem, timeItem, messageItem, nameItem, lengthItem, crcItem, summaryItem});
}

/*****************************************************
函数名称：void RtcmPage::handleCurrentFrameChanged()
入口参数：无
出口参数：无
函数功能：选中帧变化时展示该帧详情，并暂停或恢复自动跟随
*****************************************************/
void RtcmPage::handleCurrentFrameChanged()
{
    const QModelIndex currentIndex = _frameTable->currentIndex();
    if (!currentIndex.isValid())
    {
        _selectedFrameIndex = -1;
        _detailModel->removeRows(0, _detailModel->rowCount());
        _autoFollow = true;
        return;
    }

    const int row = currentIndex.row();
    _selectedFrameIndex = frameIndexOfRow(row);
    // 选中的是最新一帧时恢复自动跟随，否则保持查看历史帧。
    _autoFollow = row >= _frameModel->rowCount() - 1;
    showFrameDetail(_selectedFrameIndex);
}

/*****************************************************
函数名称：void RtcmPage::showFrameDetail(int frameIndex)
入口参数：frameIndex为帧在缓存中的下标
出口参数：无
函数功能：按分组展示指定帧的解码字段与原始数据
*****************************************************/
void RtcmPage::showFrameDetail(int frameIndex)
{
    _detailModel->removeRows(0, _detailModel->rowCount());

    if (frameIndex < 0 || frameIndex >= _frames.size())
    {
        auto* placeholder = new QStandardItem(QStringLiteral("未选择帧"));
        placeholder->setForeground(QColor(0x80, 0x80, 0x80));
        _detailModel->appendRow({placeholder, new QStandardItem(QStringLiteral("请在左侧帧列表中选择一帧"))});
        return;
    }

    const Rtcm3Frame& frame = _frames.at(frameIndex);

    // 帧基本信息。
    auto* headerItem = new QStandardItem(QStringLiteral("帧信息"));
    headerItem->appendRow({new QStandardItem(QStringLiteral("到达时间")),
                           new QStandardItem(frame.timestamp.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")))});
    headerItem->appendRow({new QStandardItem(QStringLiteral("消息号")),
                           new QStandardItem(QString::number(frame.messageNumber))});
    headerItem->appendRow({new QStandardItem(QStringLiteral("消息名称")),
                           new QStandardItem(frame.messageName)});
    headerItem->appendRow({new QStandardItem(QStringLiteral("载荷长度")),
                           new QStandardItem(QStringLiteral("%1 字节").arg(frame.payloadLength))});
    headerItem->appendRow({new QStandardItem(QStringLiteral("CRC-24Q")),
                           new QStandardItem(QStringLiteral("%1（计算 %2 / 接收 %3）")
                                                 .arg(frame.crcValid ? QStringLiteral("通过") : QStringLiteral("错误"))
                                                 .arg(frame.crcComputed, 6, 16, QLatin1Char('0'))
                                                 .arg(frame.crcReceived, 6, 16, QLatin1Char('0'))
                                                 .toUpper())});
    headerItem->appendRow({new QStandardItem(QStringLiteral("帧长度")),
                           new QStandardItem(QStringLiteral("%1 字节").arg(frame.raw.size()))});
    headerItem->appendRow({new QStandardItem(QStringLiteral("摘要")),
                           new QStandardItem(frame.summary)});
    _detailModel->appendRow(headerItem);

    // 按分组顺序追加解码字段。
    QString currentGroup;
    QStandardItem* groupItem = nullptr;
    for (const Rtcm3Field& field : frame.fields)
    {
        if (groupItem == nullptr || field.group != currentGroup)
        {
            currentGroup = field.group;
            groupItem = new QStandardItem(currentGroup.isEmpty() ? QStringLiteral("字段") : currentGroup);
            _detailModel->appendRow(groupItem);
        }
        groupItem->appendRow({new QStandardItem(field.name), new QStandardItem(field.value)});
    }

    // 原始数据按 16 字节一行展示。
    auto* rawItem = new QStandardItem(QStringLiteral("原始数据"));
    for (int offset = 0; offset < frame.raw.size(); offset += kRawBytesPerLine)
    {
        const QByteArray line = frame.raw.mid(offset, kRawBytesPerLine);
        rawItem->appendRow({new QStandardItem(QStringLiteral("%1").arg(offset, 4, 16, QLatin1Char('0')).toUpper()),
                            new QStandardItem(QString::fromLatin1(line.toHex(' ').toUpper()))});
    }
    _detailModel->appendRow(rawItem);

    _detailTree->expandAll();
}

/*****************************************************
函数名称：void RtcmPage::refreshStatistics()
入口参数：无
出口参数：无
函数功能：刷新统计页签中的计数、帧率与消息分布
*****************************************************/
void RtcmPage::refreshStatistics()
{
    QStringList lines;
    lines << QStringLiteral("累计帧数：%1").arg(_parser.frameCount());
    lines << QStringLiteral("CRC 错误帧数：%1").arg(_parser.crcErrorCount());
    lines << QStringLiteral("丢弃的噪声字节：%1").arg(_parser.droppedByteCount());
    lines << QStringLiteral("当前缓存帧数：%1 / %2").arg(_frames.size()).arg(kMaximumFrameRows);

    if (_firstFrameTime.isValid() && !_frames.isEmpty())
    {
        // 按首帧到最新帧的时间跨度计算，数据停止后帧率不会随时间衰减。
        const qint64 spanMilliseconds = _firstFrameTime.msecsTo(_frames.last().timestamp);
        lines << QStringLiteral("数据时间跨度：%1 s").arg(QString::number(spanMilliseconds / 1000.0, 'f', 3));
        if (spanMilliseconds > 0)
        {
            lines << QStringLiteral("平均帧率：%1 帧/s")
                         .arg(QString::number(_parser.frameCount() * 1000.0 / spanMilliseconds, 'f', 2));
        }
    }

    // 按消息号统计当前缓存中的帧数。
    QHash<int, int> counts;
    for (const Rtcm3Frame& frame : _frames)
    {
        counts[frame.messageNumber] += 1;
    }
    QList<QPair<int, int>> sorted;
    for (auto iterator = counts.constBegin(); iterator != counts.constEnd(); ++iterator)
    {
        sorted.append({iterator.key(), iterator.value()});
    }
    std::sort(sorted.begin(), sorted.end(), [](const QPair<int, int>& left, const QPair<int, int>& right) {
        return left.second > right.second;
    });

    lines << QString();
    lines << QStringLiteral("消息分布（按当前缓存）：");
    if (sorted.isEmpty())
    {
        lines << QStringLiteral("暂无数据");
    }
    for (const QPair<int, int>& entry : sorted)
    {
        lines << QStringLiteral("%1 (%2)  ×%3")
                     .arg(entry.first)
                     .arg(Rtcm3Parser::messageName(entry.first))
                     .arg(entry.second);
    }

    _statisticsText->setText(lines.join(QLatin1Char('\n')));
}

/*****************************************************
函数名称：void RtcmPage::refreshStatusText()
入口参数：无
出口参数：无
函数功能：刷新顶部状态文本
*****************************************************/
void RtcmPage::refreshStatusText()
{
    QString stateText;
    if (_connected)
    {
        stateText = _connectionDescription.isEmpty() ? QStringLiteral("串口已连接")
                                                    : QStringLiteral("串口已连接：%1").arg(_connectionDescription);
    }
    else
    {
        stateText = QStringLiteral("串口未连接");
    }

    _statusText->setText(QStringLiteral("%1 | 已解析 %2 帧 | CRC 错误 %3 | 噪声字节 %4")
                             .arg(stateText)
                             .arg(_parser.frameCount())
                             .arg(_parser.crcErrorCount())
                             .arg(_parser.droppedByteCount()));
}

/*****************************************************
函数名称：int RtcmPage::frameIndexOfRow(int row) const
入口参数：row为帧列表行号
出口参数：帧在缓存中的下标，越界返回 -1
函数功能：将帧列表行号转换为缓存下标
*****************************************************/
int RtcmPage::frameIndexOfRow(int row) const
{
    // 帧缓存与表格行始终保持一一对应，行号即缓存下标。
    return row >= 0 && row < _frames.size() ? row : -1;
}
