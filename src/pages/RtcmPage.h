#ifndef RTCMPAGE_H
#define RTCMPAGE_H

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QString>

#include "ElaScrollPage.h"
#include "protocol/Rtcm3Parser.h"

class ElaTableView;
class ElaText;
class ElaToolButton;
class ElaTreeView;
class QStandardItemModel;
class QTimer;

// RTCM3 解析页面：对当前串口的原始字节流做帧同步与常用消息解码。
class RtcmPage final : public ElaScrollPage
{
    Q_OBJECT
public:
    // 创建RTCM3解析页面。
    explicit RtcmPage(QWidget* parent = nullptr);
    // 析构RTCM3解析页面。
    ~RtcmPage() override;

public slots:
    // 接收串口原始字节并解析为RTCM3帧。
    void appendRawData(const QByteArray& data);
    // 更新串口连接状态提示。
    void setConnectionState(bool connected, const QString& description);

private slots:
    // 清空已解析帧与统计信息。
    void clearFrames();
    // 将待插入帧批量写入帧列表。
    void flushPendingFrames();
    // 处理帧列表当前行变化。
    void handleCurrentFrameChanged();

private:
    // 构建页面布局。
    void buildUi();
    // 构建右侧详情面板。
    QWidget* buildDetailPanel();
    // 向帧列表插入一帧。
    void insertFrameRow(const Rtcm3Frame& frame);
    // 展示指定帧的字段详情。
    void showFrameDetail(int frameIndex);
    // 刷新统计页签内容。
    void refreshStatistics();
    // 刷新顶部状态文本。
    void refreshStatusText();
    // 获取当前显示帧序号对应的缓存下标。
    int frameIndexOfRow(int row) const;

    Rtcm3Parser _parser;                       // RTCM3 字节流解析器
    QList<Rtcm3Frame> _frames;                 // 已展示帧缓存
    QList<Rtcm3Frame> _pendingFrames;          // 待插入帧列表的帧
    QStandardItemModel* _frameModel{nullptr};  // 帧列表模型
    ElaTableView* _frameTable{nullptr};        // 帧列表
    QStandardItemModel* _detailModel{nullptr}; // 字段详情模型
    ElaTreeView* _detailTree{nullptr};         // 字段详情树
    ElaText* _statisticsText{nullptr};         // 统计文本
    ElaText* _statusText{nullptr};             // 状态文本
    ElaToolButton* _clearButton{nullptr};      // 清空按钮
    QTimer* _flushTimer{nullptr};              // 批量刷新定时器
    bool _connected{false};                    // 串口连接状态
    QString _connectionDescription;            // 串口连接描述
    quint64 _frameSequence{0};                 // 帧序号计数
    QDateTime _firstFrameTime;                 // 首帧时间，用于统计帧率
    int _selectedFrameIndex{-1};               // 当前选中帧在缓存中的下标
    bool _autoFollow{true};                    // 是否自动跟随最新帧
};

#endif // RTCMPAGE_H
