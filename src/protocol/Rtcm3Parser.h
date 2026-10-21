#ifndef RTCM3PARSER_H
#define RTCM3PARSER_H

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QString>

// RTCM3 解码出的单个字段，group 用于界面分组显示。
struct Rtcm3Field
{
    QString group;  // 字段所属分组
    QString name;   // 字段名称
    QString value;  // 字段值文本
};

// RTCM3 单个完整帧的解析结果。
struct Rtcm3Frame
{
    QDateTime timestamp;       // 帧解析时间
    QByteArray raw;            // 完整原始帧，含帧头与CRC
    int messageNumber{0};      // 消息号
    QString messageName;       // 消息名称
    int payloadLength{0};      // 载荷字节数
    quint32 crcComputed{0};    // 本地计算的CRC-24Q
    quint32 crcReceived{0};    // 帧内携带的CRC-24Q
    bool crcValid{false};      // CRC校验是否通过
    QString summary;           // 帧列表使用的摘要
    QList<Rtcm3Field> fields;  // 字段级解码结果
};

// RTCM3 字节流解析器：负责帧同步、长度与CRC校验以及常用消息的字段解码。
class Rtcm3Parser
{
public:
    // 创建RTCM3字节流解析器。
    Rtcm3Parser() = default;
    // 追加原始字节并返回本次解析出的完整帧。
    QList<Rtcm3Frame> append(const QByteArray& data);
    // 清空内部缓冲与统计计数。
    void reset();
    // 获取累计解析帧数。
    quint64 frameCount() const;
    // 获取累计CRC错误帧数。
    quint64 crcErrorCount() const;
    // 获取累计丢弃的噪声字节数。
    quint64 droppedByteCount() const;
    // 计算RTCM3的CRC-24Q校验值。
    static quint32 crc24q(const QByteArray& data);
    // 获取消息号对应的中文名称。
    static QString messageName(int messageNumber);
    // 判断消息号是否为多信号消息(MSM)。
    static bool isMsmMessage(int messageNumber);
    // 获取消息号在帧列表中使用的短名称，未识别时返回消息号文本。
    static QString shortName(int messageNumber);

private:
    // 解码单帧载荷并填充消息名称、摘要与字段列表。
    static void decodePayload(Rtcm3Frame& frame);

    QByteArray _buffer;
    quint64 _frameCount{0};
    quint64 _crcErrorCount{0};
    quint64 _droppedByteCount{0};
};

#endif // RTCM3PARSER_H
