#include "pages/RtcmPage.h"

#include <QAction>
#include <QClipboard>
#include <QColor>
#include <QFrame>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QMap>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QStackedWidget>
#include <QStringList>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

#include "ElaDef.h"
#include "ElaMenu.h"
#include "ElaPivot.h"
#include "ElaScrollPageArea.h"
#include "ElaTableView.h"
#include "ElaText.h"
#include "ElaToolButton.h"
#include "ElaTreeView.h"

namespace
{
// 帧流水列号。
enum FrameColumn
{
    SequenceColumn = 0,  // 帧序号
    TimeColumn,          // 到达时间
    MessageColumn,       // 消息号
    NameColumn,          // 消息名称
    LengthColumn,        // 载荷长度
    CrcColumn,           // CRC 校验结果
    SyncColumn,          // 同步标志
    SummaryColumn,       // 摘要
    FrameColumnCount     // 列数
};

// 消息概览列号。
enum OverviewColumn
{
    OverviewMessageColumn = 0,  // 消息号
    OverviewNameColumn,         // 消息名称
    OverviewCountColumn,        // 累计次数
    OverviewRatioColumn,        // 占总帧数比例
    OverviewFrequencyColumn,    // 频率
    OverviewLastTimeColumn,     // 最后到达时间
    OverviewCrcColumn,          // CRC 错误次数
    OverviewSyncColumn,         // 最新帧同步标志
    OverviewSummaryColumn,      // 最新摘要
    OverviewColumnCount         // 列数
};

// 帧流水最多保留的帧数。
constexpr int kMaximumFrameRows = 5000;
// 帧批量插入的刷新间隔。
constexpr int kFlushIntervalMilliseconds = 100;
// 原始数据每行显示的字节数。
constexpr int kRawBytesPerLine = 16;

/*****************************************************
函数名称：QString syncText(int syncFlag)
入口参数：syncFlag为同步标志，-1表示该消息无此字段
出口参数：同步标志显示文本
函数功能：将同步标志转换为列表显示文本
*****************************************************/
QString syncText(int syncFlag)
{
    return syncFlag < 0 ? QStringLiteral("—") : QString::number(syncFlag);
}

/*****************************************************
函数名称：QList<int> indexPath(const QModelIndex& index)
入口参数：index为模型索引
出口参数：从根到该索引的行号路径
函数功能：生成用于按树形显示顺序排序的行号路径
*****************************************************/
QList<int> indexPath(const QModelIndex& index)
{
    QList<int> path;
    for (QModelIndex current = index; current.isValid(); current = current.parent())
    {
        path.prepend(current.row());
    }
    return path;
}

/*****************************************************
函数名称：void copySelectedRows(QAbstractItemView* view)
入口参数：view为表格或树视图
出口参数：无
函数功能：将选中行按制表符分列、换行分行复制到剪贴板，树节点按层级缩进
*****************************************************/
void copySelectedRows(QAbstractItemView* view)
{
    QModelIndexList rows = view->selectionModel()->selectedRows(0);
    if (rows.isEmpty() && view->currentIndex().isValid())
    {
        rows.append(view->currentIndex().siblingAtColumn(0));
    }
    std::sort(rows.begin(), rows.end(), [](const QModelIndex& left, const QModelIndex& right) {
        return indexPath(left) < indexPath(right);
    });
    QStringList lines;
    for (const QModelIndex& row : rows)
    {
        QStringList cells;
        const int columnCount = view->model()->columnCount(row.parent());
        for (int column = 0; column < columnCount; ++column)
        {
            cells.append(row.siblingAtColumn(column).data(Qt::DisplayRole).toString());
        }
        const QString indent(static_cast<int>(indexPath(row).size() - 1) * 2, QLatin1Char(' '));
        lines.append(indent + cells.join(QLatin1Char('\t')));
    }
    if (!lines.isEmpty())
    {
        QGuiApplication::clipboard()->setText(lines.join(QLatin1Char('\n')));
    }
}

// 拦截视图上的复制快捷键，改为复制整行内容。
class CopyKeyFilter final : public QObject
{
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        auto* view = qobject_cast<QAbstractItemView*>(watched);
        if (view != nullptr && event->type() == QEvent::KeyPress &&
            static_cast<QKeyEvent*>(event)->matches(QKeySequence::Copy))
        {
            copySelectedRows(view);
            return true;
        }
        return QObject::eventFilter(watched, event);
    }
};

/*****************************************************
函数名称：void installCopySupport(QAbstractItemView* view)
入口参数：view为表格或树视图
出口参数：无
函数功能：为视图添加Ctrl+C复制行与右键复制菜单
*****************************************************/
void installCopySupport(QAbstractItemView* view)
{
    view->installEventFilter(new CopyKeyFilter(view));
    view->setContextMenuPolicy(Qt::CustomContextMenu);
    QObject::connect(view, &QWidget::customContextMenuRequested, view, [view](const QPoint& pos) {
        const QModelIndex index = view->indexAt(pos);
        if (index.isValid() && !view->selectionModel()->isSelected(index))
        {
            view->setCurrentIndex(index);
        }
        ElaMenu menu(view);
        QAction* cellAction = menu.addAction(QStringLiteral("复制单元格"));
        cellAction->setEnabled(index.isValid());
        QAction* rowAction = menu.addAction(QStringLiteral("复制行"));
        rowAction->setShortcut(QKeySequence::Copy);
        rowAction->setEnabled(view->currentIndex().isValid() || view->selectionModel()->hasSelection());
        QAction* chosen = menu.exec(view->viewport()->mapToGlobal(pos));
        if (chosen == cellAction)
        {
            QGuiApplication::clipboard()->setText(index.data(Qt::DisplayRole).toString());
        }
        else if (chosen == rowAction)
        {
            copySelectedRows(view);
        }
    });
}
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
函数功能：构建统计卡片、左侧页签与详情面板布局
*****************************************************/
void RtcmPage::buildUi()
{
    setTitleVisible(false);
    setWindowTitle(QStringLiteral("RTCM3 解析"));

    auto* pageContent = new QWidget(this);
    auto* pageLayout = new QVBoxLayout(pageContent);
    pageLayout->setContentsMargins(8, 8, 8, 8);
    pageLayout->setSpacing(8);

    auto* headerLayout = new QHBoxLayout();
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(8);
    _statusText = new ElaText(QStringLiteral("串口未连接"), 13, pageContent);
    headerLayout->addWidget(_statusText, 1);
    _clearButton = new ElaToolButton(pageContent);
    _clearButton->setText(QStringLiteral("清空"));
    _clearButton->setElaIcon(ElaIconType::TrashCanList);
    _clearButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    _clearButton->setFixedHeight(30);
    _clearButton->setToolTip(QStringLiteral("清空已解析的帧与统计信息"));
    headerLayout->addWidget(_clearButton);
    pageLayout->addLayout(headerLayout);

    auto* summaryLayout = new QHBoxLayout();
    summaryLayout->setContentsMargins(0, 0, 0, 0);
    summaryLayout->setSpacing(8);
    const QStringList summaryTitles = {QStringLiteral("累计帧数"), QStringLiteral("平均帧率"),
                                       QStringLiteral("CRC 错误"), QStringLiteral("噪声字节"),
                                       QStringLiteral("消息类型")};
    for (const QString& title : summaryTitles)
    {
        auto* area = new ElaScrollPageArea(pageContent);
        area->setFixedHeight(64);
        auto* layout = new QVBoxLayout(area);
        layout->setContentsMargins(12, 6, 12, 6);
        layout->setSpacing(0);
        auto* titleText = new ElaText(title, 12, area);
        auto* valueText = new ElaText(QStringLiteral("0"), 20, area);
        valueText->setStyleSheet(QStringLiteral("font-weight: 600;"));
        layout->addWidget(titleText);
        layout->addWidget(valueText);
        summaryLayout->addWidget(area, 1);
        _summaryValues.append(valueText);
    }
    pageLayout->addLayout(summaryLayout);

    _frameModel = new QStandardItemModel(this);
    _frameModel->setHorizontalHeaderLabels({QStringLiteral("序号"), QStringLiteral("时间"), QStringLiteral("消息"),
                                            QStringLiteral("名称"), QStringLiteral("长度"), QStringLiteral("CRC"),
                                            QStringLiteral("Sync"), QStringLiteral("摘要")});
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
    _frameTable->setColumnWidth(SyncColumn, 46);
    _frameTable->model()->setHeaderData(SyncColumn, Qt::Horizontal,
                                        QStringLiteral("观测量同步标志或 MSM 多消息标志：1 表示同一历元还有后续消息"),
                                        Qt::ToolTipRole);
    _frameTable->setMinimumWidth(440);
    installCopySupport(_frameTable);

    _messageModel = new QStandardItemModel(this);
    _messageModel->setHorizontalHeaderLabels({QStringLiteral("消息号"), QStringLiteral("名称"), QStringLiteral("次数"),
                                               QStringLiteral("占比"), QStringLiteral("频率(Hz)"),
                                               QStringLiteral("最后到达"), QStringLiteral("CRC错误"),
                                               QStringLiteral("Sync"), QStringLiteral("最新摘要")});
    _messageModel->setHeaderData(OverviewSyncColumn, Qt::Horizontal,
                                 QStringLiteral("最新一帧的观测量同步标志或 MSM 多消息标志"), Qt::ToolTipRole);
    _messageTable = new ElaTableView(pageContent);
    _messageTable->setModel(_messageModel);
    _messageTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    _messageTable->setSelectionMode(QAbstractItemView::SingleSelection);
    _messageTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    _messageTable->setAlternatingRowColors(true);
    _messageTable->setWordWrap(false);
    _messageTable->verticalHeader()->setVisible(false);
    _messageTable->verticalHeader()->setDefaultSectionSize(26);
    _messageTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    _messageTable->horizontalHeader()->setSectionResizeMode(OverviewSummaryColumn, QHeaderView::Stretch);
    _messageTable->setColumnWidth(OverviewMessageColumn, 70);
    _messageTable->setColumnWidth(OverviewNameColumn, 128);
    _messageTable->setColumnWidth(OverviewCountColumn, 64);
    _messageTable->setColumnWidth(OverviewRatioColumn, 70);
    _messageTable->setColumnWidth(OverviewFrequencyColumn, 86);
    _messageTable->setColumnWidth(OverviewLastTimeColumn, 160);
    _messageTable->setColumnWidth(OverviewCrcColumn, 70);
    _messageTable->setColumnWidth(OverviewSyncColumn, 46);
    _messageTable->setMinimumWidth(440);
    installCopySupport(_messageTable);

    auto* pivot = new ElaPivot(pageContent);
    pivot->setFixedHeight(34);
    pivot->setTextPixelSize(13);
    pivot->setPivotSpacing(6);
    pivot->setMarkWidth(52);
    pivot->appendPivot(QStringLiteral("消息概览"));
    pivot->appendPivot(QStringLiteral("帧流水"));
    _leftStack = new QStackedWidget(pageContent);
    _leftStack->addWidget(_messageTable);
    _leftStack->addWidget(_frameTable);
    auto* leftPanel = new QWidget(pageContent);
    auto* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(4);
    leftLayout->addWidget(pivot);
    leftLayout->addWidget(_leftStack, 1);

    auto* splitter = new QSplitter(Qt::Horizontal, pageContent);
    splitter->setChildrenCollapsible(false);
    splitter->setHandleWidth(8);
    splitter->addWidget(leftPanel);
    splitter->addWidget(buildDetailPanel());
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 0);
    splitter->setSizes({640, 360});
    pageLayout->addWidget(splitter, 1);
    addCentralWidget(pageContent, true, false, 0);

    _flushTimer = new QTimer(this);
    _flushTimer->setSingleShot(true);
    _flushTimer->setInterval(kFlushIntervalMilliseconds);
    connect(_flushTimer, &QTimer::timeout, this, &RtcmPage::flushPendingFrames);
    connect(_clearButton, &ElaToolButton::clicked, this, &RtcmPage::clearFrames);
    connect(_frameTable->selectionModel(), &QItemSelectionModel::currentRowChanged,
            this, &RtcmPage::handleCurrentFrameChanged);
    connect(_messageTable->selectionModel(), &QItemSelectionModel::currentRowChanged,
            this, &RtcmPage::handleCurrentMessageChanged);
    connect(pivot, &ElaPivot::pivotClicked, this, [this](int index) {
        _leftStack->setCurrentIndex(index);
        // 切回帧流水且处于自动跟随时，立即定位到最新帧，选择信号会刷新详情。
        if (index == 1 && _autoFollow && _frameModel->rowCount() > 0)
        {
            _frameTable->setCurrentIndex(_frameModel->index(_frameModel->rowCount() - 1, 0));
            _frameTable->scrollToBottom();
        }
        refreshCurrentDetail();
    });

    refreshStatusText();
    refreshSummaryCards();
    showFrameDetail(nullptr);
}

/*****************************************************
函数名称：QWidget* RtcmPage::buildDetailPanel()
入口参数：无
出口参数：详情面板控件
函数功能：构建右侧帧详情面板
*****************************************************/
QWidget* RtcmPage::buildDetailPanel()
{
    auto* panel = new QFrame(this);
    panel->setFrameShape(QFrame::StyledPanel);
    panel->setMinimumWidth(280);
    auto* panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(8, 7, 8, 8);
    panelLayout->setSpacing(5);
    auto* title = new ElaText(QStringLiteral("帧详情"), 14, panel);
    title->setStyleSheet(QStringLiteral("font-weight: 600;"));
    _detailSubtitle = new ElaText(QStringLiteral("未选择帧"), 12, panel);
    panelLayout->addWidget(title);
    panelLayout->addWidget(_detailSubtitle);

    _detailModel = new QStandardItemModel(this);
    _detailModel->setHorizontalHeaderLabels({QStringLiteral("字段"), QStringLiteral("值")});
    _detailTree = new ElaTreeView(panel);
    _detailTree->setModel(_detailModel);
    _detailTree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    _detailTree->setSelectionBehavior(QAbstractItemView::SelectRows);
    _detailTree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    installCopySupport(_detailTree);
    _detailTree->setAlternatingRowColors(true);
    _detailTree->setUniformRowHeights(true);
    _detailTree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    _detailTree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    panelLayout->addWidget(_detailTree, 1);
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
    _messageStats.clear();
    _frameModel->removeRows(0, _frameModel->rowCount());
    _messageModel->removeRows(0, _messageModel->rowCount());
    _selectedFrameIndex = -1;
    _selectedMessageNumber = -1;
    _frameSequence = 0;
    _firstFrameTime = QDateTime();
    _lastFrameTime = QDateTime();
    _autoFollow = true;
    _frameTable->selectionModel()->clear();
    _messageTable->selectionModel()->clear();
    showFrameDetail(nullptr);
    refreshSummaryCards();
    refreshStatusText();
}

/*****************************************************
函数名称：void RtcmPage::flushPendingFrames()
入口参数：无
出口参数：无
函数功能：将排队的帧批量插入帧列表并维护累计统计
*****************************************************/
void RtcmPage::flushPendingFrames()
{
    QMap<int, bool> changedMessages;
    if (!_pendingFrames.isEmpty())
    {
        for (const Rtcm3Frame& frame : _pendingFrames)
        {
            auto& stat = _messageStats[frame.messageNumber];
            ++stat.count;
            if (!frame.crcValid)
            {
                ++stat.crcErrorCount;
            }
            if (!stat.firstTime.isValid())
            {
                stat.firstTime = frame.timestamp;
            }
            stat.lastTime = frame.timestamp;
            stat.latestFrame = frame;
            changedMessages[frame.messageNumber] = true;
            _lastFrameTime = frame.timestamp;
            if (!_firstFrameTime.isValid())
            {
                _firstFrameTime = frame.timestamp;
            }
            insertFrameRow(frame);
            _frames.append(frame);
        }
        _pendingFrames.clear();
        // 总帧数变化后所有消息的占比都会变化，因此逐行刷新整张概览表（消息种类通常只有几十种）。
        for (auto iterator = _messageStats.constBegin(); iterator != _messageStats.constEnd(); ++iterator)
        {
            updateMessageOverviewRow(iterator.key());
        }

        const int excess = _frames.size() - kMaximumFrameRows;
        if (excess > 0)
        {
            const QSignalBlocker selectionBlocker(_frameTable->selectionModel());
            _frames.erase(_frames.begin(), _frames.begin() + excess);
            _frameModel->removeRows(0, excess);
            if (_selectedFrameIndex >= 0)
            {
                _selectedFrameIndex -= excess;
                if (_selectedFrameIndex < 0)
                {
                    _selectedFrameIndex = -1;
                    _frameTable->selectionModel()->clear();
                    _autoFollow = true;
                    if (_leftStack->currentIndex() == 1)
                    {
                        showFrameDetail(nullptr);
                    }
                }
            }
        }

        if (_autoFollow && _leftStack->currentIndex() == 1 && _frameModel->rowCount() > 0)
        {
            _frameTable->setCurrentIndex(_frameModel->index(_frameModel->rowCount() - 1, 0));
            _frameTable->scrollToBottom();
        }
        if (_leftStack->currentIndex() == 0 && _selectedMessageNumber >= 0 && changedMessages.contains(_selectedMessageNumber))
        {
            refreshCurrentDetail();
        }
    }
    refreshSummaryCards();
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
    auto* syncItem = new QStandardItem(syncText(frame.syncFlag));
    syncItem->setTextAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
    _frameModel->appendRow({sequenceItem, timeItem, messageItem, nameItem, lengthItem, crcItem, syncItem,
                            new QStandardItem(frame.summary)});
}

/*****************************************************
函数名称：void RtcmPage::updateMessageOverviewRow(int messageNumber)
入口参数：messageNumber为消息号
出口参数：无
函数功能：更新或按消息号升序插入消息概览行
*****************************************************/
void RtcmPage::updateMessageOverviewRow(int messageNumber)
{
    const auto statIterator = _messageStats.constFind(messageNumber);
    if (statIterator == _messageStats.constEnd())
    {
        return;
    }
    const MessageStat& stat = statIterator.value();
    int row = -1;
    for (int index = 0; index < _messageModel->rowCount(); ++index)
    {
        if (_messageModel->index(index, OverviewMessageColumn).data(Qt::UserRole).toInt() == messageNumber)
        {
            row = index;
            break;
        }
    }
    if (row < 0)
    {
        row = 0;
        while (row < _messageModel->rowCount() &&
               _messageModel->index(row, OverviewMessageColumn).data(Qt::UserRole).toInt() < messageNumber)
        {
            ++row;
        }
        _messageModel->insertRow(row);
        auto* numberItem = new QStandardItem;
        numberItem->setData(messageNumber, Qt::UserRole);
        _messageModel->setItem(row, OverviewMessageColumn, numberItem);
        for (int column = OverviewNameColumn; column < OverviewColumnCount; ++column)
        {
            _messageModel->setItem(row, column, new QStandardItem);
        }
    }
    const double ratio = _parser.frameCount() > 0 ? stat.count * 100.0 / _parser.frameCount() : 0.0;
    const qint64 span = stat.firstTime.msecsTo(stat.lastTime);
    const QString frequency = stat.count < 2 || span <= 0 ? QStringLiteral("—")
                                                           : QString::number((stat.count - 1) * 1000.0 / span, 'f', 2);
    const QStringList values = {QString::number(messageNumber), stat.latestFrame.messageName, QString::number(stat.count),
                                QStringLiteral("%1%").arg(QString::number(ratio, 'f', 1)), frequency,
                                stat.lastTime.toString(QStringLiteral("HH:mm:ss.zzz")), QString::number(stat.crcErrorCount),
                                syncText(stat.latestFrame.syncFlag), stat.latestFrame.summary};
    for (int column = 0; column < OverviewColumnCount; ++column)
    {
        auto* item = _messageModel->item(row, column);
        item->setText(values.at(column));
        if (column == OverviewMessageColumn || column == OverviewCountColumn || column == OverviewRatioColumn ||
            column == OverviewFrequencyColumn || column == OverviewCrcColumn)
        {
            item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        }
        if (column == OverviewSyncColumn)
        {
            item->setTextAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
        }
        if (column == OverviewCrcColumn)
        {
            item->setForeground(stat.crcErrorCount > 0 ? QColor(0xD9, 0x3B, 0x3B) : QBrush());
        }
    }
}

/*****************************************************
函数名称：void RtcmPage::handleCurrentFrameChanged()
入口参数：无
出口参数：无
函数功能：选中帧流水行变化时展示详情并暂停或恢复自动跟随
*****************************************************/
void RtcmPage::handleCurrentFrameChanged()
{
    if (_leftStack->currentIndex() != 1)
    {
        return;
    }
    const QModelIndex currentIndex = _frameTable->currentIndex();
    if (!currentIndex.isValid())
    {
        _selectedFrameIndex = -1;
        _autoFollow = true;
        showFrameDetail(nullptr);
        return;
    }
    const int row = currentIndex.row();
    _selectedFrameIndex = frameIndexOfRow(row);
    _autoFollow = row >= _frameModel->rowCount() - 1;
    refreshCurrentDetail();
}

/*****************************************************
函数名称：void RtcmPage::handleCurrentMessageChanged()
入口参数：无
出口参数：无
函数功能：选中消息概览行时展示该消息的最新帧
*****************************************************/
void RtcmPage::handleCurrentMessageChanged()
{
    if (_leftStack->currentIndex() != 0)
    {
        return;
    }
    const QModelIndex currentIndex = _messageTable->currentIndex();
    _selectedMessageNumber = currentIndex.isValid()
                                 ? currentIndex.data(Qt::UserRole).toInt()
                                 : -1;
    refreshCurrentDetail();
}

/*****************************************************
函数名称：void RtcmPage::refreshCurrentDetail()
入口参数：无
出口参数：无
函数功能：根据当前页签的选中项刷新详情面板
*****************************************************/
void RtcmPage::refreshCurrentDetail()
{
    if (_leftStack->currentIndex() == 0)
    {
        const auto iterator = _messageStats.constFind(_selectedMessageNumber);
        showFrameDetail(iterator == _messageStats.constEnd() ? nullptr : &iterator.value().latestFrame);
        return;
    }
    const int index = _selectedFrameIndex;
    showFrameDetail(index >= 0 && index < _frames.size() ? &_frames.at(index) : nullptr);
}

/*****************************************************
函数名称：void RtcmPage::showFrameDetail(const Rtcm3Frame* frame)
入口参数：frame为待展示帧，nullptr表示未选择
出口参数：无
函数功能：按分组展示指定帧的解码字段与原始数据
*****************************************************/
void RtcmPage::showFrameDetail(const Rtcm3Frame* frame)
{
    _detailModel->removeRows(0, _detailModel->rowCount());
    if (frame == nullptr)
    {
        _detailSubtitle->setText(QStringLiteral("未选择帧"));
        auto* placeholder = new QStandardItem(QStringLiteral("未选择帧"));
        placeholder->setForeground(QColor(0x80, 0x80, 0x80));
        _detailModel->appendRow({placeholder, new QStandardItem(QStringLiteral("请在左侧列表中选择一帧"))});
        return;
    }
    _detailSubtitle->setText(QStringLiteral("消息 %1 · %2 · %3")
                                 .arg(frame->messageNumber)
                                 .arg(frame->messageName)
                                 .arg(frame->timestamp.toString(QStringLiteral("HH:mm:ss.zzz"))));
    auto* headerItem = new QStandardItem(QStringLiteral("帧信息"));
    headerItem->appendRow({new QStandardItem(QStringLiteral("到达时间")), new QStandardItem(frame->timestamp.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")))});
    headerItem->appendRow({new QStandardItem(QStringLiteral("消息号")), new QStandardItem(QString::number(frame->messageNumber))});
    headerItem->appendRow({new QStandardItem(QStringLiteral("消息名称")), new QStandardItem(frame->messageName)});
    headerItem->appendRow({new QStandardItem(QStringLiteral("载荷长度")), new QStandardItem(QStringLiteral("%1 字节").arg(frame->payloadLength))});
    headerItem->appendRow({new QStandardItem(QStringLiteral("CRC-24Q")), new QStandardItem(QStringLiteral("%1（计算 %2 / 接收 %3）").arg(frame->crcValid ? QStringLiteral("通过") : QStringLiteral("错误")).arg(frame->crcComputed, 6, 16, QLatin1Char('0')).arg(frame->crcReceived, 6, 16, QLatin1Char('0')).toUpper())});
    headerItem->appendRow({new QStandardItem(QStringLiteral("Sync")), new QStandardItem(syncText(frame->syncFlag))});
    headerItem->appendRow({new QStandardItem(QStringLiteral("帧长度")), new QStandardItem(QStringLiteral("%1 字节").arg(frame->raw.size()))});
    headerItem->appendRow({new QStandardItem(QStringLiteral("摘要")), new QStandardItem(frame->summary)});
    _detailModel->appendRow(headerItem);
    QString currentGroup;
    QStandardItem* groupItem = nullptr;
    for (const Rtcm3Field& field : frame->fields)
    {
        if (groupItem == nullptr || field.group != currentGroup)
        {
            currentGroup = field.group;
            groupItem = new QStandardItem(currentGroup.isEmpty() ? QStringLiteral("字段") : currentGroup);
            _detailModel->appendRow(groupItem);
        }
        groupItem->appendRow({new QStandardItem(field.name), new QStandardItem(field.value)});
    }
    auto* rawItem = new QStandardItem(QStringLiteral("原始数据"));
    for (int offset = 0; offset < frame->raw.size(); offset += kRawBytesPerLine)
    {
        const QByteArray line = frame->raw.mid(offset, kRawBytesPerLine);
        rawItem->appendRow({new QStandardItem(QStringLiteral("%1").arg(offset, 4, 16, QLatin1Char('0')).toUpper()),
                            new QStandardItem(QString::fromLatin1(line.toHex(' ').toUpper()))});
    }
    _detailModel->appendRow(rawItem);
    _detailTree->expandAll();
}

/*****************************************************
函数名称：void RtcmPage::refreshSummaryCards()
入口参数：无
出口参数：无
函数功能：刷新顶部统计卡片
*****************************************************/
void RtcmPage::refreshSummaryCards()
{
    const quint64 frameCount = _parser.frameCount();
    QString averageRate = QStringLiteral("—");
    if (frameCount > 0 && _firstFrameTime.isValid() && _lastFrameTime.isValid())
    {
        const qint64 span = _firstFrameTime.msecsTo(_lastFrameTime);
        if (span > 0)
        {
            averageRate = QStringLiteral("%1 帧/s").arg(QString::number(frameCount * 1000.0 / span, 'f', 2));
        }
    }
    const quint64 crcErrors = _parser.crcErrorCount();
    const double crcRatio = frameCount > 0 ? crcErrors * 100.0 / frameCount : 0.0;
    _summaryValues.at(0)->setText(QString::number(frameCount));
    _summaryValues.at(1)->setText(averageRate);
    _summaryValues.at(2)->setText(QStringLiteral("%1（%2%）").arg(crcErrors).arg(QString::number(crcRatio, 'f', 1)));
    _summaryValues.at(2)->setStyleSheet(crcErrors > 0 ? QStringLiteral("font-weight: 600; color: #D93B3B;")
                                                        : QStringLiteral("font-weight: 600;"));
    _summaryValues.at(3)->setText(QString::number(_parser.droppedByteCount()));
    _summaryValues.at(4)->setText(QString::number(_messageStats.size()));
}

/*****************************************************
函数名称：void RtcmPage::refreshStatusText()
入口参数：无
出口参数：无
函数功能：刷新顶部连接状态文本
*****************************************************/
void RtcmPage::refreshStatusText()
{
    if (_connected)
    {
        _statusText->setText(_connectionDescription.isEmpty() ? QStringLiteral("串口已连接")
                                                               : QStringLiteral("串口已连接：%1").arg(_connectionDescription));
    }
    else
    {
        _statusText->setText(QStringLiteral("串口未连接"));
    }
}

/*****************************************************
函数名称：int RtcmPage::frameIndexOfRow(int row) const
入口参数：row为帧列表行号
出口参数：帧在缓存中的下标，越界返回 -1
函数功能：将帧列表行号转换为缓存下标
*****************************************************/
int RtcmPage::frameIndexOfRow(int row) const
{
    return row >= 0 && row < _frames.size() ? row : -1;
}
