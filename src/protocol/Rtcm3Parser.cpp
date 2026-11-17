#include "protocol/Rtcm3Parser.h"

#include <QHash>
#include <QStringList>

#include <cmath>

namespace
{
// RTCM3 帧头字节。
constexpr quint8 kPreamble = 0xD3;
// 长度域为 10 位，单帧载荷最大 1023 字节。
constexpr int kMaximumPayloadLength = 1023;
// 半圆到度的换算系数。
constexpr double kSemicircleToDegree = 57.29577951308232;
// WGS84 长半轴。
constexpr double kWgs84SemiMajorAxis = 6378137.0;
// WGS84 扁率。
constexpr double kWgs84Flattening = 1.0 / 298.257223563;

/*****************************************************
函数名称：QString flagText(quint64 value)
入口参数：value —— 位域数值
出口参数：中文是否文本
函数功能：将 0/1 标志位转换为中文文本
*****************************************************/
QString flagText(quint64 value)
{
    return value == 0 ? QStringLiteral("0 (否)") : QStringLiteral("1 (是)");
}

/*****************************************************
函数名称：QString formatNumber(double value, int decimals)
入口参数：value —— 待格式化数值；decimals —— 定点小数位
出口参数：格式化后的文本
函数功能：按数量级自动选择定点或科学计数法，避免极小值显示为零
*****************************************************/
QString formatNumber(double value, int decimals)
{
    const double magnitude = std::fabs(value);
    if (magnitude != 0.0 && (magnitude < 1e-3 || magnitude >= 1e7))
    {
        return QString::number(value, 'e', 6);
    }
    return QString::number(value, 'f', decimals);
}

/*****************************************************
函数名称：QString meters(double value)
入口参数：value —— 米为单位的数值
出口参数：带单位的文本
函数功能：格式化以米为单位的数值
*****************************************************/
QString meters(double value)
{
    return QStringLiteral("%1 m").arg(formatNumber(value, 4));
}

/*****************************************************
函数名称：QString degrees(double value)
入口参数：value —— 度为单位的数值
出口参数：带单位的文本
函数功能：格式化以度为单位的数值
*****************************************************/
QString degrees(double value)
{
    return QStringLiteral("%1 °").arg(QString::number(value, 'f', 9));
}

/*****************************************************
函数名称：QString satelliteLabel(const QString& prefix, int number)
入口参数：prefix —— 星座前缀；number —— 卫星号
出口参数：卫星标签文本
函数功能：生成如 G12、R05 的卫星标签
*****************************************************/
QString satelliteLabel(const QString& prefix, int number)
{
    return QStringLiteral("%1%2").arg(prefix).arg(number, 2, 10, QLatin1Char('0'));
}

// RTCM3 载荷按位读取的读取器，位序为 MSB 优先。
class BitReader
{
public:
    // 使用载荷字节构造读取器。
    explicit BitReader(const QByteArray& payload) : _payload(payload) {}

    // 读取无符号位域，越界位补 0。
    quint64 readBits(int bits)
    {
        quint64 value = 0;
        for (int index = 0; index < bits; ++index)
        {
            value = (value << 1) | quint64(readBit());
        }
        return value;
    }

    // 读取二进制补码有符号位域。
    qint64 readSigned(int bits)
    {
        const quint64 raw = readBits(bits);
        if (bits <= 0 || bits >= 64)
        {
            return qint64(raw);
        }
        const quint64 signBit = quint64(1) << (bits - 1);
        if ((raw & signBit) != 0)
        {
            return qint64(raw | (~quint64(0) << bits));
        }
        return qint64(raw);
    }

    // 读取符号-数值表示的有符号位域（GLONASS 使用）。
    double readSignMagnitude(int bits)
    {
        const int sign = readBit();
        const quint64 magnitude = readBits(bits - 1);
        return sign == 1 ? -double(magnitude) : double(magnitude);
    }

    // 读取指定字节数的定长字符串。
    QString readCharacters(int count)
    {
        QByteArray text;
        text.reserve(count);
        for (int index = 0; index < count; ++index)
        {
            text.append(char(readBits(8)));
        }
        return QString::fromLatin1(text).trimmed();
    }

    // 跳过若干位。
    void skip(int bits) { _position += bits; }

    // 获取已读取的位数。
    int position() const { return _position; }

    // 获取剩余可读位数。
    int remainingBits() const { return _payload.size() * 8 - _position; }

private:
    // 读取单个位，越界时返回 0。
    int readBit()
    {
        if (_position >= _payload.size() * 8)
        {
            ++_position;
            return 0;
        }
        const int byteIndex = _position >> 3;
        const int bitIndex = 7 - (_position & 7);
        ++_position;
        return (static_cast<quint8>(_payload.at(byteIndex)) >> bitIndex) & 0x01;
    }

    QByteArray _payload;  // 帧载荷
    int _position{0};     // 当前位偏移
};

// 字段收集器，按分组顺序记录解码结果。
struct FieldCollector
{
    QList<Rtcm3Field>* fields{nullptr};  // 输出字段列表
    QString group;                       // 当前分组名称
    int syncFlag{-1};                    // 同步/多消息标志，-1 表示该消息无此字段

    // 切换后续字段所属分组。
    void setGroup(const QString& name) { group = name; }

    // 追加一个字段。
    void add(const QString& name, const QString& value)
    {
        fields->append(Rtcm3Field{group, name, value});
    }
};

/*****************************************************
函数名称：void ecefToGeodetic(double x, double y, double z, double& latitude, double& longitude, double& height)
入口参数：x/y/z —— ECEF 坐标；latitude/longitude/height —— 输出的大地坐标
出口参数：无
函数功能：将 WGS84 地心直角坐标换算为经纬度与大地高
*****************************************************/
void ecefToGeodetic(double x, double y, double z, double& latitude, double& longitude, double& height)
{
    const double eccentricitySquared = kWgs84Flattening * (2.0 - kWgs84Flattening);
    const double semiMinorAxis = kWgs84SemiMajorAxis * (1.0 - kWgs84Flattening);
    const double secondEccentricitySquared =
        (kWgs84SemiMajorAxis * kWgs84SemiMajorAxis - semiMinorAxis * semiMinorAxis) /
        (semiMinorAxis * semiMinorAxis);
    const double horizontal = std::sqrt(x * x + y * y);
    const double theta = std::atan2(z * kWgs84SemiMajorAxis, horizontal * semiMinorAxis);
    const double sinTheta = std::sin(theta);
    const double cosTheta = std::cos(theta);
    latitude = std::atan2(z + secondEccentricitySquared * semiMinorAxis * sinTheta * sinTheta * sinTheta,
                          horizontal - eccentricitySquared * kWgs84SemiMajorAxis * cosTheta * cosTheta * cosTheta);
    longitude = std::atan2(y, x);
    const double sinLatitude = std::sin(latitude);
    const double curvatureRadius =
        kWgs84SemiMajorAxis / std::sqrt(1.0 - eccentricitySquared * sinLatitude * sinLatitude);
    height = horizontal / std::cos(latitude) - curvatureRadius;
}

// MSM 星座信息。
struct MsmSystem
{
    QString name;                          // 星座名称
    QString prefix;                        // 卫星号前缀
    int prnOffset{0};                      // 卫星掩码位到 PRN 的偏移
    const char* const* signalTable;        // 32 项信号名称表
};

// 以下信号标识表按「信号 ID 直接索引」，索引 0 为占位，索引 1~32 对应掩码位号。
// GPS 信号标识表（RINEX 码型）。
const char* const kMsmSignalsGps[33] = {
    "", "1C", "1P", "1W", "1Y", "1M", "1N", "2C",
    "2P", "2W", "2Y", "2M", "2N", "5I", "5Q", "5X",
    "", "", "", "", "", "", "", "",
    "", "", "", "", "", "", "", "", ""};

// GLONASS 信号标识表。
const char* const kMsmSignalsGlonass[33] = {
    "", "1C", "1P", "2C", "2P", "", "", "",
    "", "", "", "", "", "", "", "",
    "", "", "", "", "", "", "", "",
    "", "", "", "", "", "", "", "", ""};

// Galileo 信号标识表。
const char* const kMsmSignalsGalileo[33] = {
    "", "1C", "1A", "1B", "1X", "1Z", "6C", "6A",
    "6B", "6X", "6Z", "7I", "7Q", "7X", "8I", "8Q",
    "8X", "", "", "", "", "", "", "",
    "", "", "", "", "", "", "", "", ""};

// SBAS 信号标识表。
const char* const kMsmSignalsSbas[33] = {
    "", "1C", "5I", "5Q", "5X", "", "", "",
    "", "", "", "", "", "", "", "",
    "", "", "", "", "", "", "", "",
    "", "", "", "", "", "", "", "", ""};

// QZSS 信号标识表。
const char* const kMsmSignalsQzss[33] = {
    "", "1C", "1S", "1L", "1X", "1Z", "2S", "2L",
    "2X", "2Z", "5I", "5Q", "5X", "5Z", "6S", "6L",
    "6X", "6Z", "", "", "", "", "", "",
    "", "", "", "", "", "", "", "", ""};

// 北斗信号标识表。
const char* const kMsmSignalsBeidou[33] = {
    "", "1I", "1Q", "1X", "7I", "7Q", "7X", "6I",
    "6Q", "6X", "", "", "", "", "", "",
    "", "", "", "", "", "", "", "",
    "", "", "", "", "", "", "", "", ""};

// NavIC(IRNSS) 信号标识表。
const char* const kMsmSignalsNavic[33] = {
    "", "5A", "5B", "5C", "5X", "9A", "9B", "9C",
    "9X", "", "", "", "", "", "", "",
    "", "", "", "", "", "", "", "",
    "", "", "", "", "", "", "", "", ""};

/*****************************************************
函数名称：bool msmSystemFor(int family, MsmSystem& system)
入口参数：family —— MSM 消息号除以 10 的星座标识；system —— 输出星座信息
出口参数：星座是否识别
函数功能：按 MSM 消息族查找星座信息
*****************************************************/
bool msmSystemFor(int family, MsmSystem& system)
{
    switch (family)
    {
    case 107:
        system = MsmSystem{QStringLiteral("GPS"), QStringLiteral("G"), 0, kMsmSignalsGps};
        return true;
    case 108:
        system = MsmSystem{QStringLiteral("GLONASS"), QStringLiteral("R"), 0, kMsmSignalsGlonass};
        return true;
    case 109:
        system = MsmSystem{QStringLiteral("Galileo"), QStringLiteral("E"), 0, kMsmSignalsGalileo};
        return true;
    case 110:
        system = MsmSystem{QStringLiteral("SBAS"), QStringLiteral("S"), 119, kMsmSignalsSbas};
        return true;
    case 111:
        system = MsmSystem{QStringLiteral("QZSS"), QStringLiteral("J"), 192, kMsmSignalsQzss};
        return true;
    case 112:
        system = MsmSystem{QStringLiteral("北斗"), QStringLiteral("C"), 0, kMsmSignalsBeidou};
        return true;
    case 113:
        system = MsmSystem{QStringLiteral("NavIC"), QStringLiteral("I"), 0, kMsmSignalsNavic};
        return true;
    default:
        return false;
    }
}

/*****************************************************
函数名称：QString msmTypeText(int msmType)
入口参数：msmType —— MSM 类型号 1~7
出口参数：MSM 类型说明
函数功能：说明各 MSM 类型携带的观测量
*****************************************************/
QString msmTypeText(int msmType)
{
    switch (msmType)
    {
    case 1:
        return QStringLiteral("伪距");
    case 2:
        return QStringLiteral("载波相位");
    case 3:
        return QStringLiteral("伪距+载波相位");
    case 4:
        return QStringLiteral("伪距+载波相位+载噪比");
    case 5:
        return QStringLiteral("伪距+载波相位+多普勒+载噪比");
    case 6:
        return QStringLiteral("高分辨率伪距+载波相位+载噪比");
    case 7:
        return QStringLiteral("高分辨率伪距+载波相位+多普勒+载噪比");
    default:
        return QStringLiteral("未知");
    }
}

// 以下为各消息的解码实现，返回帧列表使用的摘要文本。
QString decodeStationCoordinates(BitReader& reader, FieldCollector& out, bool withAntennaHeight);
QString decodeAntennaDescriptor(BitReader& reader, FieldCollector& out, bool withSerialNumber);
QString decodeReceiverDescriptor(BitReader& reader, FieldCollector& out);
QString decodeSystemParameters(BitReader& reader, FieldCollector& out);
QString decodeUnicodeText(BitReader& reader, FieldCollector& out);
QString decodeGpsObservables(BitReader& reader, FieldCollector& out, int messageNumber);
QString decodeGlonassObservables(BitReader& reader, FieldCollector& out, int messageNumber);
QString decodeGpsEphemeris(BitReader& reader, FieldCollector& out);
QString decodeGlonassEphemeris(BitReader& reader, FieldCollector& out);
QString decodeBeidouEphemeris(BitReader& reader, FieldCollector& out);
QString decodeQzssEphemeris(BitReader& reader, FieldCollector& out);
QString decodeGalileoEphemeris(BitReader& reader, FieldCollector& out, bool withSecondFrequency);
QString decodeGlonassCodePhaseBiases(BitReader& reader, FieldCollector& out);
QString decodeMsm(BitReader& reader, FieldCollector& out, int messageNumber);
/*****************************************************
函数名称：QString decodeStationCoordinates(BitReader& reader, FieldCollector& out, bool withAntennaHeight)
入口参数：reader —— 位读取器；out —— 字段收集器；withAntennaHeight —— 是否解码天线高
出口参数：帧摘要文本
函数功能：解码 1005/1006 基站坐标消息
*****************************************************/
QString decodeStationCoordinates(BitReader& reader, FieldCollector& out, bool withAntennaHeight)
{
    const quint64 stationId = reader.readBits(12);
    const quint64 itrfYear = reader.readBits(6);
    const quint64 gpsIndicator = reader.readBits(1);
    const quint64 glonassIndicator = reader.readBits(1);
    const quint64 galileoIndicator = reader.readBits(1);
    const quint64 referenceStationIndicator = reader.readBits(1);
    const double ecefX = double(reader.readSigned(38)) * 0.0001;
    const quint64 singleOscillator = reader.readBits(1);
    reader.skip(1);
    const double ecefY = double(reader.readSigned(38)) * 0.0001;
    const quint64 quarterCycle = reader.readBits(2);
    const double ecefZ = double(reader.readSigned(38)) * 0.0001;
    const double antennaHeight =
        withAntennaHeight ? double(reader.readBits(16)) * 0.0001 : 0.0;

    out.setGroup(QStringLiteral("基站信息"));
    out.add(QStringLiteral("基站 ID"), QString::number(stationId));
    out.add(QStringLiteral("ITRF 实现年"), QStringLiteral("%1 (ITRF%2)").arg(itrfYear).arg(itrfYear <= 10 ? 2008 - itrfYear : 0));
    out.add(QStringLiteral("提供 GPS 数据"), flagText(gpsIndicator));
    out.add(QStringLiteral("提供 GLONASS 数据"), flagText(glonassIndicator));
    out.add(QStringLiteral("提供 Galileo 数据"), flagText(galileoIndicator));
    out.add(QStringLiteral("参考站指示位"), QString::number(referenceStationIndicator));
    out.add(QStringLiteral("单接收机振荡器"), QString::number(singleOscillator));
    out.add(QStringLiteral("四分之一周指示"), QString::number(quarterCycle));

    out.setGroup(QStringLiteral("天线参考点 (ECEF)"));
    out.add(QStringLiteral("X"), meters(ecefX));
    out.add(QStringLiteral("Y"), meters(ecefY));
    out.add(QStringLiteral("Z"), meters(ecefZ));
    if (withAntennaHeight)
    {
        out.add(QStringLiteral("天线高"), meters(antennaHeight));
    }

    double latitude = 0.0;
    double longitude = 0.0;
    double height = 0.0;
    ecefToGeodetic(ecefX, ecefY, ecefZ, latitude, longitude, height);
    out.setGroup(QStringLiteral("换算为 WGS84 大地坐标"));
    out.add(QStringLiteral("纬度"), degrees(latitude * 180.0 / 3.14159265358979323846));
    out.add(QStringLiteral("经度"), degrees(longitude * 180.0 / 3.14159265358979323846));
    out.add(QStringLiteral("大地高"), meters(height));

    return QStringLiteral("X %1 Y %2 Z %3")
        .arg(formatNumber(ecefX, 3), formatNumber(ecefY, 3), formatNumber(ecefZ, 3));
}

/*****************************************************
函数名称：QString decodeAntennaDescriptor(BitReader& reader, FieldCollector& out, bool withSerialNumber)
入口参数：reader —— 位读取器；out —— 字段收集器；withSerialNumber —— 是否解码序列号
出口参数：帧摘要文本
函数功能：解码 1007/1008 天线描述消息
*****************************************************/
QString decodeAntennaDescriptor(BitReader& reader, FieldCollector& out, bool withSerialNumber)
{
    const quint64 stationId = reader.readBits(12);
    const int descriptorCount = int(reader.readBits(8));
    const QString descriptor = reader.readCharacters(descriptorCount);
    const quint64 setupId = reader.readBits(8);

    out.setGroup(QStringLiteral("天线信息"));
    out.add(QStringLiteral("基站 ID"), QString::number(stationId));
    out.add(QStringLiteral("天线描述"), descriptor);
    out.add(QStringLiteral("安装标识"), QString::number(setupId));
    if (withSerialNumber)
    {
        const int serialCount = int(reader.readBits(8));
        out.add(QStringLiteral("天线序列号"), reader.readCharacters(serialCount));
    }
    return descriptor;
}

/*****************************************************
函数名称：QString decodeReceiverDescriptor(BitReader& reader, FieldCollector& out)
入口参数：reader —— 位读取器；out —— 字段收集器
出口参数：帧摘要文本
函数功能：解码 1033 接收机与天线描述消息
*****************************************************/
QString decodeReceiverDescriptor(BitReader& reader, FieldCollector& out)
{
    const quint64 stationId = reader.readBits(12);
    const QString descriptor = reader.readCharacters(int(reader.readBits(8)));
    const quint64 setupId = reader.readBits(8);
    const QString antennaSerial = reader.readCharacters(int(reader.readBits(8)));
    const QString receiverType = reader.readCharacters(int(reader.readBits(8)));
    const QString firmware = reader.readCharacters(int(reader.readBits(8)));
    const QString receiverSerial = reader.readCharacters(int(reader.readBits(8)));

    out.setGroup(QStringLiteral("接收机与天线"));
    out.add(QStringLiteral("基站 ID"), QString::number(stationId));
    out.add(QStringLiteral("天线描述"), descriptor);
    out.add(QStringLiteral("安装标识"), QString::number(setupId));
    out.add(QStringLiteral("天线序列号"), antennaSerial);
    out.add(QStringLiteral("接收机型号"), receiverType);
    out.add(QStringLiteral("固件版本"), firmware);
    out.add(QStringLiteral("接收机序列号"), receiverSerial);
    return QStringLiteral("%1 / %2").arg(receiverType, descriptor);
}

/*****************************************************
函数名称：QString decodeSystemParameters(BitReader& reader, FieldCollector& out)
入口参数：reader —— 位读取器；out —— 字段收集器
出口参数：帧摘要文本
函数功能：解码 1013 系统参数消息
*****************************************************/
QString decodeSystemParameters(BitReader& reader, FieldCollector& out)
{
    const quint64 stationId = reader.readBits(12);
    const quint64 modifiedJulianDay = reader.readBits(16);
    const double secondsOfDay = double(reader.readBits(17));
    const quint64 announcementCount = reader.readBits(5);
    const quint64 leapSeconds = reader.readBits(8);

    out.setGroup(QStringLiteral("系统参数"));
    out.add(QStringLiteral("基站 ID"), QString::number(stationId));
    out.add(QStringLiteral("简化儒略日"), QString::number(modifiedJulianDay));
    out.add(QStringLiteral("当日秒 (UTC)"), formatNumber(secondsOfDay, 0));
    out.add(QStringLiteral("闰秒"), QStringLiteral("%1 s").arg(leapSeconds));
    out.add(QStringLiteral("消息通告条数"), QString::number(announcementCount));

    for (quint64 index = 0; index < announcementCount; ++index)
    {
        const quint64 announcedMessage = reader.readBits(12);
        const quint64 synchronousFlag = reader.readBits(1);
        const double interval = double(reader.readBits(16)) * 0.1;
        out.setGroup(QStringLiteral("消息通告 %1").arg(index + 1));
        out.add(QStringLiteral("消息号"), QStringLiteral("%1 (%2)").arg(announcedMessage).arg(Rtcm3Parser::messageName(int(announcedMessage))));
        out.add(QStringLiteral("同步标志"), flagText(synchronousFlag));
        out.add(QStringLiteral("发送间隔"), QStringLiteral("%1 s").arg(formatNumber(interval, 1)));
    }

    return QStringLiteral("MJD %1 闰秒 %2").arg(modifiedJulianDay).arg(leapSeconds);
}

/*****************************************************
函数名称：QString decodeUnicodeText(BitReader& reader, FieldCollector& out)
入口参数：reader —— 位读取器；out —— 字段收集器
出口参数：帧摘要文本
函数功能：解码 1029 Unicode 文本消息
*****************************************************/
QString decodeUnicodeText(BitReader& reader, FieldCollector& out)
{
    const quint64 stationId = reader.readBits(12);
    const quint64 modifiedJulianDay = reader.readBits(16);
    const double secondsOfDay = double(reader.readBits(17));
    const quint64 characterCount = reader.readBits(7);
    const quint64 utf8UnitCount = reader.readBits(8);

    QByteArray text;
    text.reserve(int(utf8UnitCount));
    for (quint64 index = 0; index < utf8UnitCount; ++index)
    {
        text.append(char(reader.readBits(8)));
    }
    const QString decoded = QString::fromUtf8(text);

    out.setGroup(QStringLiteral("文本信息"));
    out.add(QStringLiteral("基站 ID"), QString::number(stationId));
    out.add(QStringLiteral("简化儒略日"), QString::number(modifiedJulianDay));
    out.add(QStringLiteral("当日秒 (UTC)"), formatNumber(secondsOfDay, 0));
    out.add(QStringLiteral("字符数"), QString::number(characterCount));
    out.add(QStringLiteral("UTF-8 字节数"), QString::number(utf8UnitCount));
    out.add(QStringLiteral("文本内容"), decoded);
    return decoded;
}

/*****************************************************
函数名称：QString decodeGpsObservables(BitReader& reader, FieldCollector& out, int messageNumber)
入口参数：reader —— 位读取器；out —— 字段收集器；messageNumber —— 消息号
出口参数：帧摘要文本
函数功能：解码 1001~1004 GPS 观测量消息
*****************************************************/
QString decodeGpsObservables(BitReader& reader, FieldCollector& out, int messageNumber)
{
    const quint64 stationId = reader.readBits(12);
    const double tow = double(reader.readBits(30)) * 0.001;
    const quint64 synchronousFlag = reader.readBits(1);
    const quint64 satelliteCount = reader.readBits(5);
    const quint64 smoothingIndicator = reader.readBits(1);
    const quint64 smoothingInterval = reader.readBits(3);

    out.setGroup(QStringLiteral("帧头"));
    out.add(QStringLiteral("基站 ID"), QString::number(stationId));
    out.add(QStringLiteral("历元时间 (TOW)"), QStringLiteral("%1 s").arg(formatNumber(tow, 3)));
    out.add(QStringLiteral("同步标志"), flagText(synchronousFlag));
    out.syncFlag = int(synchronousFlag);
    out.add(QStringLiteral("卫星数"), QString::number(satelliteCount));
    out.add(QStringLiteral("平滑指示"), flagText(smoothingIndicator));
    out.add(QStringLiteral("平滑间隔（索引）"), QString::number(smoothingInterval));

    for (quint64 index = 0; index < satelliteCount; ++index)
    {
        const quint64 prn = reader.readBits(6);
        const quint64 codeIndicator = reader.readBits(1);
        const double pseudorange = double(reader.readBits(24)) * 0.02;
        const double phaseDifference = double(reader.readSigned(20)) * 0.0005;
        const quint64 lockTime = reader.readBits(7);

        out.setGroup(QStringLiteral("卫星 %1").arg(satelliteLabel(QStringLiteral("G"), int(prn))));
        out.add(QStringLiteral("L1 码型"), codeIndicator == 0 ? QStringLiteral("0 (C/A)") : QStringLiteral("1 (P)"));
        out.add(QStringLiteral("L1 伪距"), meters(pseudorange));
        out.add(QStringLiteral("L1 相位差"), meters(phaseDifference));
        out.add(QStringLiteral("L1 锁定时间"), QString::number(lockTime));

        if (messageNumber == 1002 || messageNumber == 1004)
        {
            const quint64 ambiguity = reader.readBits(8);
            const double carrierToNoise = double(reader.readBits(8)) * 0.25;
            out.add(QStringLiteral("L1 整周模糊度"), QString::number(ambiguity));
            out.add(QStringLiteral("L1 载噪比"), QStringLiteral("%1 dBHz").arg(formatNumber(carrierToNoise, 2)));
        }

        if (messageNumber == 1003 || messageNumber == 1004)
        {
            const quint64 l2CodeIndicator = reader.readBits(2);
            const double l2PseudorangeDifference = double(reader.readSigned(14)) * 0.02;
            const double l2PhaseDifference = double(reader.readSigned(20)) * 0.0005;
            const quint64 l2LockTime = reader.readBits(7);
            out.add(QStringLiteral("L2 码型"), QString::number(l2CodeIndicator));
            out.add(QStringLiteral("L2-L1 伪距差"), meters(l2PseudorangeDifference));
            out.add(QStringLiteral("L2 相位差"), meters(l2PhaseDifference));
            out.add(QStringLiteral("L2 锁定时间"), QString::number(l2LockTime));

            if (messageNumber == 1004)
            {
                const double l2CarrierToNoise = double(reader.readBits(8)) * 0.25;
                out.add(QStringLiteral("L2 载噪比"), QStringLiteral("%1 dBHz").arg(formatNumber(l2CarrierToNoise, 2)));
            }
        }
    }

    return QStringLiteral("%1 颗卫星").arg(satelliteCount);
}

/*****************************************************
函数名称：QString decodeGlonassObservables(BitReader& reader, FieldCollector& out, int messageNumber)
入口参数：reader —— 位读取器；out —— 字段收集器；messageNumber —— 消息号
出口参数：帧摘要文本
函数功能：解码 1009~1012 GLONASS 观测量消息
*****************************************************/
QString decodeGlonassObservables(BitReader& reader, FieldCollector& out, int messageNumber)
{
    const quint64 stationId = reader.readBits(12);
    const double timeOfDay = double(reader.readBits(27)) * 0.001;
    const quint64 synchronousFlag = reader.readBits(1);
    const quint64 satelliteCount = reader.readBits(5);
    const quint64 smoothingIndicator = reader.readBits(1);
    const quint64 smoothingInterval = reader.readBits(3);

    out.setGroup(QStringLiteral("帧头"));
    out.add(QStringLiteral("基站 ID"), QString::number(stationId));
    out.add(QStringLiteral("历元时间 (TOD)"), QStringLiteral("%1 s").arg(formatNumber(timeOfDay, 3)));
    out.add(QStringLiteral("同步标志"), flagText(synchronousFlag));
    out.syncFlag = int(synchronousFlag);
    out.add(QStringLiteral("卫星数"), QString::number(satelliteCount));
    out.add(QStringLiteral("平滑指示"), flagText(smoothingIndicator));
    out.add(QStringLiteral("平滑间隔（索引）"), QString::number(smoothingInterval));

    for (quint64 index = 0; index < satelliteCount; ++index)
    {
        const quint64 prn = reader.readBits(6);
        const quint64 codeIndicator = reader.readBits(1);
        const quint64 frequencyRaw = reader.readBits(5);
        const double pseudorange = double(reader.readBits(25)) * 0.02;
        const double phaseDifference = double(reader.readSigned(20)) * 0.0005;
        const quint64 lockTime = reader.readBits(7);

        out.setGroup(QStringLiteral("卫星 %1").arg(satelliteLabel(QStringLiteral("R"), int(prn))));
        out.add(QStringLiteral("L1 码型"), codeIndicator == 0 ? QStringLiteral("0 (C/A)") : QStringLiteral("1 (P)"));
        out.add(QStringLiteral("频点号"), QStringLiteral("%1 (原始值 %2)").arg(qint64(frequencyRaw) - 7).arg(frequencyRaw));
        out.add(QStringLiteral("L1 伪距"), meters(pseudorange));
        out.add(QStringLiteral("L1 相位差"), meters(phaseDifference));
        out.add(QStringLiteral("L1 锁定时间"), QString::number(lockTime));

        if (messageNumber == 1010 || messageNumber == 1012)
        {
            const quint64 ambiguity = reader.readBits(7);
            const double carrierToNoise = double(reader.readBits(8)) * 0.25;
            out.add(QStringLiteral("L1 整周模糊度"), QString::number(ambiguity));
            out.add(QStringLiteral("L1 载噪比"), QStringLiteral("%1 dBHz").arg(formatNumber(carrierToNoise, 2)));
        }

        if (messageNumber == 1011 || messageNumber == 1012)
        {
            const quint64 l2CodeIndicator = reader.readBits(2);
            const double l2PseudorangeDifference = double(reader.readSigned(14)) * 0.02;
            const double l2PhaseDifference = double(reader.readSigned(20)) * 0.0005;
            const quint64 l2LockTime = reader.readBits(7);
            out.add(QStringLiteral("L2 码型"), QString::number(l2CodeIndicator));
            out.add(QStringLiteral("L2-L1 伪距差"), meters(l2PseudorangeDifference));
            out.add(QStringLiteral("L2 相位差"), meters(l2PhaseDifference));
            out.add(QStringLiteral("L2 锁定时间"), QString::number(l2LockTime));

            if (messageNumber == 1012)
            {
                const double l2CarrierToNoise = double(reader.readBits(8)) * 0.25;
                out.add(QStringLiteral("L2 载噪比"), QStringLiteral("%1 dBHz").arg(formatNumber(l2CarrierToNoise, 2)));
            }
        }
    }

    return QStringLiteral("%1 颗卫星").arg(satelliteCount);
}

/*****************************************************
函数名称：QString decodeGpsEphemeris(BitReader& reader, FieldCollector& out)
入口参数：reader —— 位读取器；out —— 字段收集器
出口参数：帧摘要文本
函数功能：解码 1019 GPS 星历消息
*****************************************************/
QString decodeGpsEphemeris(BitReader& reader, FieldCollector& out)
{
    const quint64 prn = reader.readBits(6);
    const quint64 week = reader.readBits(10);
    const quint64 svAccuracy = reader.readBits(4);
    const quint64 l2Code = reader.readBits(2);
    const double idot = double(reader.readSigned(14)) * std::ldexp(1.0, -43) * kSemicircleToDegree;
    const quint64 iode = reader.readBits(8);
    const double toc = double(reader.readBits(16)) * 16.0;
    const double af2 = double(reader.readSigned(8)) * std::ldexp(1.0, -55);
    const double af1 = double(reader.readSigned(16)) * std::ldexp(1.0, -43);
    const double af0 = double(reader.readSigned(22)) * std::ldexp(1.0, -31);
    const quint64 iodc = reader.readBits(10);
    const double crs = double(reader.readSigned(16)) * std::ldexp(1.0, -5);
    const double deltaN = double(reader.readSigned(16)) * std::ldexp(1.0, -43) * kSemicircleToDegree;
    const double m0 = double(reader.readSigned(32)) * std::ldexp(1.0, -31) * kSemicircleToDegree;
    const double cuc = double(reader.readSigned(16)) * std::ldexp(1.0, -29);
    const double eccentricity = double(reader.readBits(32)) * std::ldexp(1.0, -33);
    const double cus = double(reader.readSigned(16)) * std::ldexp(1.0, -29);
    const double sqrtSemiMajorAxis = double(reader.readBits(32)) * std::ldexp(1.0, -19);
    const double toe = double(reader.readBits(16)) * 16.0;
    const double cic = double(reader.readSigned(16)) * std::ldexp(1.0, -29);
    const double omega0 = double(reader.readSigned(32)) * std::ldexp(1.0, -31) * kSemicircleToDegree;
    const double cis = double(reader.readSigned(16)) * std::ldexp(1.0, -29);
    const double i0 = double(reader.readSigned(32)) * std::ldexp(1.0, -31) * kSemicircleToDegree;
    const double crc = double(reader.readSigned(16)) * std::ldexp(1.0, -5);
    const double omega = double(reader.readSigned(32)) * std::ldexp(1.0, -31) * kSemicircleToDegree;
    const double omegaDot = double(reader.readSigned(24)) * std::ldexp(1.0, -43) * kSemicircleToDegree;
    const double groupDelay = double(reader.readSigned(8)) * std::ldexp(1.0, -31);
    const quint64 svHealth = reader.readBits(6);
    const quint64 l2PDataFlag = reader.readBits(1);
    const quint64 fitInterval = reader.readBits(1);

    out.setGroup(QStringLiteral("卫星与时间"));
    out.add(QStringLiteral("卫星号"), satelliteLabel(QStringLiteral("G"), int(prn)));
    out.add(QStringLiteral("GPS 周"), QString::number(week));
    out.add(QStringLiteral("toe"), QStringLiteral("%1 s").arg(formatNumber(toe, 0)));
    out.add(QStringLiteral("toc"), QStringLiteral("%1 s").arg(formatNumber(toc, 0)));
    out.add(QStringLiteral("IODE"), QString::number(iode));
    out.add(QStringLiteral("IODC"), QString::number(iodc));
    out.add(QStringLiteral("拟合区间"), fitInterval == 0 ? QStringLiteral("0 (4 小时)") : QStringLiteral("1 (大于 4 小时)"));

    out.setGroup(QStringLiteral("时钟参数"));
    out.add(QStringLiteral("af0"), QStringLiteral("%1 s").arg(formatNumber(af0, 12)));
    out.add(QStringLiteral("af1"), QStringLiteral("%1 s/s").arg(formatNumber(af1, 14)));
    out.add(QStringLiteral("af2"), QStringLiteral("%1 s/s²").arg(formatNumber(af2, 16)));
    out.add(QStringLiteral("群延迟 TGD"), QStringLiteral("%1 s").arg(formatNumber(groupDelay, 12)));

    out.setGroup(QStringLiteral("轨道参数"));
    out.add(QStringLiteral("√A"), QStringLiteral("%1 √m").arg(formatNumber(sqrtSemiMajorAxis, 6)));
    out.add(QStringLiteral("偏心率 e"), formatNumber(eccentricity, 12));
    out.add(QStringLiteral("参考时刻倾角 i0"), degrees(i0));
    out.add(QStringLiteral("升交点经度 Ω0"), degrees(omega0));
    out.add(QStringLiteral("近地点角距 ω"), degrees(omega));
    out.add(QStringLiteral("平近点角 M0"), degrees(m0));
    out.add(QStringLiteral("平均角速度改正 Δn"), QStringLiteral("%1 °/s").arg(formatNumber(deltaN, 12)));
    out.add(QStringLiteral("升交点赤经变化率 ΩDOT"), QStringLiteral("%1 °/s").arg(formatNumber(omegaDot, 12)));
    out.add(QStringLiteral("倾角变化率 IDOT"), QStringLiteral("%1 °/s").arg(formatNumber(idot, 12)));
    out.add(QStringLiteral("Cuc"), QStringLiteral("%1 rad").arg(formatNumber(cuc, 12)));
    out.add(QStringLiteral("Cus"), QStringLiteral("%1 rad").arg(formatNumber(cus, 12)));
    out.add(QStringLiteral("Cic"), QStringLiteral("%1 rad").arg(formatNumber(cic, 12)));
    out.add(QStringLiteral("Cis"), QStringLiteral("%1 rad").arg(formatNumber(cis, 12)));
    out.add(QStringLiteral("Crc"), meters(crc));
    out.add(QStringLiteral("Crs"), meters(crs));

    out.setGroup(QStringLiteral("健康与标志"));
    out.add(QStringLiteral("URA 精度指数"), QString::number(svAccuracy));
    out.add(QStringLiteral("卫星健康"), QString::number(svHealth));
    out.add(QStringLiteral("L2 码型"), QString::number(l2Code));
    out.add(QStringLiteral("L2 P 数据标志"), flagText(l2PDataFlag));

    return QStringLiteral("PRN %1 周 %2 toe %3").arg(prn).arg(week).arg(formatNumber(toe, 0));
}

/*****************************************************
函数名称：QString decodeGlonassEphemeris(BitReader& reader, FieldCollector& out)
入口参数：reader —— 位读取器；out —— 字段收集器
出口参数：帧摘要文本
函数功能：解码 1020 GLONASS 星历消息
*****************************************************/
QString decodeGlonassEphemeris(BitReader& reader, FieldCollector& out)
{
    const quint64 prn = reader.readBits(6);
    const quint64 frequencyRaw = reader.readBits(5);
    const quint64 almanacHealth = reader.readBits(1);
    const quint64 almanacHealthAvailability = reader.readBits(1);
    const quint64 p1 = reader.readBits(2);
    const quint64 tkHours = reader.readBits(5);
    const quint64 tkMinutes = reader.readBits(6);
    const quint64 tkSeconds = reader.readBits(1);
    const quint64 bnMsb = reader.readBits(1);
    const quint64 p2 = reader.readBits(1);
    const quint64 tb = reader.readBits(7);

    const double xVelocity = reader.readSignMagnitude(24) * std::ldexp(1.0, -20) * 1000.0;
    const double xPosition = reader.readSignMagnitude(27) * std::ldexp(1.0, -11) * 1000.0;
    const double xAcceleration = reader.readSignMagnitude(5) * std::ldexp(1.0, -30) * 1000.0;
    const double yVelocity = reader.readSignMagnitude(24) * std::ldexp(1.0, -20) * 1000.0;
    const double yPosition = reader.readSignMagnitude(27) * std::ldexp(1.0, -11) * 1000.0;
    const double yAcceleration = reader.readSignMagnitude(5) * std::ldexp(1.0, -30) * 1000.0;
    const double zVelocity = reader.readSignMagnitude(24) * std::ldexp(1.0, -20) * 1000.0;
    const double zPosition = reader.readSignMagnitude(27) * std::ldexp(1.0, -11) * 1000.0;
    const double zAcceleration = reader.readSignMagnitude(5) * std::ldexp(1.0, -30) * 1000.0;

    const quint64 p3 = reader.readBits(1);
    const double frequencyBias = reader.readSignMagnitude(11) * std::ldexp(1.0, -40);
    const quint64 mp = reader.readBits(2);
    const quint64 lnThirdString = reader.readBits(1);
    const double tauN = reader.readSignMagnitude(22) * std::ldexp(1.0, -30);
    const double deltaTauN = double(reader.readSigned(5)) * std::ldexp(1.0, -30);
    const quint64 en = reader.readBits(5);
    const quint64 p4 = reader.readBits(1);
    const quint64 ft = reader.readBits(4);
    const quint64 nt = reader.readBits(11);
    const quint64 m = reader.readBits(2);
    const quint64 additionalDataAvailability = reader.readBits(1);
    const quint64 na = reader.readBits(11);
    const double tauC = double(reader.readSigned(32)) * std::ldexp(1.0, -31);
    const quint64 n4 = reader.readBits(5);
    const double tauGps = reader.readSignMagnitude(22) * std::ldexp(1.0, -30);
    const quint64 lnFifthString = reader.readBits(1);

    out.setGroup(QStringLiteral("卫星与时间"));
    out.add(QStringLiteral("卫星号"), satelliteLabel(QStringLiteral("R"), int(prn)));
    out.add(QStringLiteral("频点号"), QStringLiteral("%1 (原始值 %2)").arg(qint64(frequencyRaw) - 7).arg(frequencyRaw));
    out.add(QStringLiteral("参考时刻 tb"), QStringLiteral("%1 min").arg(tb * 15));
    out.add(QStringLiteral("历元时刻 tk"), QStringLiteral("%1:%2:%3")
                                                  .arg(tkHours, 2, 10, QLatin1Char('0'))
                                                  .arg(tkMinutes, 2, 10, QLatin1Char('0'))
                                                  .arg(tkSeconds * 30, 2, 10, QLatin1Char('0')));
    out.add(QStringLiteral("星历健康 Cn"), QString::number(almanacHealth));
    out.add(QStringLiteral("健康可用性"), flagText(almanacHealthAvailability));
    out.add(QStringLiteral("P1"), QString::number(p1));
    out.add(QStringLiteral("P2"), QString::number(p2));
    out.add(QStringLiteral("Bn MSB"), QString::number(bnMsb));

    out.setGroup(QStringLiteral("位置"));
    out.add(QStringLiteral("X"), meters(xPosition));
    out.add(QStringLiteral("Y"), meters(yPosition));
    out.add(QStringLiteral("Z"), meters(zPosition));

    out.setGroup(QStringLiteral("速度"));
    out.add(QStringLiteral("VX"), QStringLiteral("%1 m/s").arg(formatNumber(xVelocity, 6)));
    out.add(QStringLiteral("VY"), QStringLiteral("%1 m/s").arg(formatNumber(yVelocity, 6)));
    out.add(QStringLiteral("VZ"), QStringLiteral("%1 m/s").arg(formatNumber(zVelocity, 6)));

    out.setGroup(QStringLiteral("加速度"));
    out.add(QStringLiteral("AX"), QStringLiteral("%1 m/s²").arg(formatNumber(xAcceleration, 9)));
    out.add(QStringLiteral("AY"), QStringLiteral("%1 m/s²").arg(formatNumber(yAcceleration, 9)));
    out.add(QStringLiteral("AZ"), QStringLiteral("%1 m/s²").arg(formatNumber(zAcceleration, 9)));

    out.setGroup(QStringLiteral("时钟与附加参数"));
    out.add(QStringLiteral("相对频率偏差 γn"), QStringLiteral("%1").arg(formatNumber(frequencyBias, 14)));
    out.add(QStringLiteral("时钟偏差 τn"), QStringLiteral("%1 s").arg(formatNumber(tauN, 14)));
    out.add(QStringLiteral("τn 变化量 Δτn"), QStringLiteral("%1 s").arg(formatNumber(deltaTauN, 14)));
    out.add(QStringLiteral("年龄 En"), QString::number(en));
    out.add(QStringLiteral("τc"), QStringLiteral("%1 s").arg(formatNumber(tauC, 14)));
    out.add(QStringLiteral("τGPS"), QStringLiteral("%1 s").arg(formatNumber(tauGps, 14)));
    out.add(QStringLiteral("N4"), QString::number(n4));
    out.add(QStringLiteral("M(P)"), QString::number(mp));
    out.add(QStringLiteral("P3"), QString::number(p3));
    out.add(QStringLiteral("P4"), QString::number(p4));
    out.add(QStringLiteral("FT"), QString::number(ft));
    out.add(QStringLiteral("NT"), QString::number(nt));
    out.add(QStringLiteral("M"), QString::number(m));
    out.add(QStringLiteral("附加数据可用"), flagText(additionalDataAvailability));
    out.add(QStringLiteral("NA"), QString::number(na));
    out.add(QStringLiteral("第三条字符串标识"), QString::number(lnThirdString));
    out.add(QStringLiteral("第五条字符串标识"), QString::number(lnFifthString));

    return QStringLiteral("PRN %1 tb %2 min").arg(prn).arg(tb * 15);
}

/*****************************************************
函数名称：QString decodeBeidouEphemeris(BitReader& reader, FieldCollector& out)
入口参数：reader —— 位读取器；out —— 字段收集器
出口参数：帧摘要文本
函数功能：解码 1042 北斗星历消息
*****************************************************/
QString decodeBeidouEphemeris(BitReader& reader, FieldCollector& out)
{
    const quint64 prn = reader.readBits(6);
    const quint64 week = reader.readBits(13);
    const quint64 userRangeAccuracy = reader.readBits(4);
    const double idot = double(reader.readSigned(14)) * std::ldexp(1.0, -43) * kSemicircleToDegree;
    const quint64 aode = reader.readBits(5);
    const double toc = double(reader.readBits(17)) * 8.0;
    const double af2 = double(reader.readSigned(11)) * std::ldexp(1.0, -66);
    const double af1 = double(reader.readSigned(22)) * std::ldexp(1.0, -50);
    const double af0 = double(reader.readSigned(24)) * std::ldexp(1.0, -33);
    const quint64 aodc = reader.readBits(5);
    const double crs = double(reader.readSigned(18)) * std::ldexp(1.0, -6);
    const double deltaN = double(reader.readSigned(16)) * std::ldexp(1.0, -43) * kSemicircleToDegree;
    const double m0 = double(reader.readSigned(32)) * std::ldexp(1.0, -31) * kSemicircleToDegree;
    const double cuc = double(reader.readSigned(18)) * std::ldexp(1.0, -31);
    const double eccentricity = double(reader.readBits(32)) * std::ldexp(1.0, -33);
    const double cus = double(reader.readSigned(18)) * std::ldexp(1.0, -31);
    const double sqrtSemiMajorAxis = double(reader.readBits(32)) * std::ldexp(1.0, -19);
    const double toe = double(reader.readBits(17)) * 8.0;
    const double cic = double(reader.readSigned(18)) * std::ldexp(1.0, -31);
    const double omega0 = double(reader.readSigned(32)) * std::ldexp(1.0, -31) * kSemicircleToDegree;
    const double cis = double(reader.readSigned(18)) * std::ldexp(1.0, -31);
    const double i0 = double(reader.readSigned(32)) * std::ldexp(1.0, -31) * kSemicircleToDegree;
    const double crc = double(reader.readSigned(18)) * std::ldexp(1.0, -6);
    const double omega = double(reader.readSigned(32)) * std::ldexp(1.0, -31) * kSemicircleToDegree;
    const double omegaDot = double(reader.readSigned(24)) * std::ldexp(1.0, -43) * kSemicircleToDegree;
    const double groupDelay1 = double(reader.readSigned(10)) * 1e-10;
    const double groupDelay2 = double(reader.readSigned(10)) * 1e-10;
    const quint64 svHealth = reader.readBits(1);

    out.setGroup(QStringLiteral("卫星与时间"));
    out.add(QStringLiteral("卫星号"), satelliteLabel(QStringLiteral("C"), int(prn)));
    out.add(QStringLiteral("北斗周"), QString::number(week));
    out.add(QStringLiteral("toe"), QStringLiteral("%1 s").arg(formatNumber(toe, 0)));
    out.add(QStringLiteral("toc"), QStringLiteral("%1 s").arg(formatNumber(toc, 0)));
    out.add(QStringLiteral("AODE"), QString::number(aode));
    out.add(QStringLiteral("AODC"), QString::number(aodc));

    out.setGroup(QStringLiteral("时钟参数"));
    out.add(QStringLiteral("af0"), QStringLiteral("%1 s").arg(formatNumber(af0, 12)));
    out.add(QStringLiteral("af1"), QStringLiteral("%1 s/s").arg(formatNumber(af1, 14)));
    out.add(QStringLiteral("af2"), QStringLiteral("%1 s/s²").arg(formatNumber(af2, 16)));
    out.add(QStringLiteral("TGD1"), QStringLiteral("%1 s").arg(formatNumber(groupDelay1, 13)));
    out.add(QStringLiteral("TGD2"), QStringLiteral("%1 s").arg(formatNumber(groupDelay2, 13)));

    out.setGroup(QStringLiteral("轨道参数"));
    out.add(QStringLiteral("√A"), QStringLiteral("%1 √m").arg(formatNumber(sqrtSemiMajorAxis, 6)));
    out.add(QStringLiteral("偏心率 e"), formatNumber(eccentricity, 12));
    out.add(QStringLiteral("参考时刻倾角 i0"), degrees(i0));
    out.add(QStringLiteral("升交点经度 Ω0"), degrees(omega0));
    out.add(QStringLiteral("近地点角距 ω"), degrees(omega));
    out.add(QStringLiteral("平近点角 M0"), degrees(m0));
    out.add(QStringLiteral("平均角速度改正 Δn"), QStringLiteral("%1 °/s").arg(formatNumber(deltaN, 12)));
    out.add(QStringLiteral("升交点赤经变化率 ΩDOT"), QStringLiteral("%1 °/s").arg(formatNumber(omegaDot, 12)));
    out.add(QStringLiteral("倾角变化率 IDOT"), QStringLiteral("%1 °/s").arg(formatNumber(idot, 12)));
    out.add(QStringLiteral("Cuc"), QStringLiteral("%1 rad").arg(formatNumber(cuc, 12)));
    out.add(QStringLiteral("Cus"), QStringLiteral("%1 rad").arg(formatNumber(cus, 12)));
    out.add(QStringLiteral("Cic"), QStringLiteral("%1 rad").arg(formatNumber(cic, 12)));
    out.add(QStringLiteral("Cis"), QStringLiteral("%1 rad").arg(formatNumber(cis, 12)));
    out.add(QStringLiteral("Crc"), meters(crc));
    out.add(QStringLiteral("Crs"), meters(crs));

    out.setGroup(QStringLiteral("健康与标志"));
    out.add(QStringLiteral("URA 指数"), QString::number(userRangeAccuracy));
    out.add(QStringLiteral("卫星健康"), QString::number(svHealth));

    return QStringLiteral("PRN %1 周 %2 toe %3").arg(prn).arg(week).arg(formatNumber(toe, 0));
}

/*****************************************************
函数名称：QString decodeQzssEphemeris(BitReader& reader, FieldCollector& out)
入口参数：reader —— 位读取器；out —— 字段收集器
出口参数：帧摘要文本
函数功能：解码 1044 QZSS 星历消息
*****************************************************/
QString decodeQzssEphemeris(BitReader& reader, FieldCollector& out)
{
    const quint64 prnRaw = reader.readBits(4);
    const quint64 prn = prnRaw + 192;
    const double toc = double(reader.readBits(16)) * 16.0;
    const double af2 = double(reader.readSigned(8)) * std::ldexp(1.0, -55);
    const double af1 = double(reader.readSigned(16)) * std::ldexp(1.0, -43);
    const double af0 = double(reader.readSigned(22)) * std::ldexp(1.0, -31);
    const quint64 iode = reader.readBits(8);
    const double crs = double(reader.readSigned(16)) * std::ldexp(1.0, -5);
    const double deltaN = double(reader.readSigned(16)) * std::ldexp(1.0, -43) * kSemicircleToDegree;
    const double m0 = double(reader.readSigned(32)) * std::ldexp(1.0, -31) * kSemicircleToDegree;
    const double cuc = double(reader.readSigned(16)) * std::ldexp(1.0, -29);
    const double eccentricity = double(reader.readBits(32)) * std::ldexp(1.0, -33);
    const double cus = double(reader.readSigned(16)) * std::ldexp(1.0, -29);
    const double sqrtSemiMajorAxis = double(reader.readBits(32)) * std::ldexp(1.0, -19);
    const double toe = double(reader.readBits(16)) * 16.0;
    const double cic = double(reader.readSigned(16)) * std::ldexp(1.0, -29);
    const double omega0 = double(reader.readSigned(32)) * std::ldexp(1.0, -31) * kSemicircleToDegree;
    const double cis = double(reader.readSigned(16)) * std::ldexp(1.0, -29);
    const double i0 = double(reader.readSigned(32)) * std::ldexp(1.0, -31) * kSemicircleToDegree;
    const double crc = double(reader.readSigned(16)) * std::ldexp(1.0, -5);
    const double omega = double(reader.readSigned(32)) * std::ldexp(1.0, -31) * kSemicircleToDegree;
    const double omegaDot = double(reader.readSigned(24)) * std::ldexp(1.0, -43) * kSemicircleToDegree;
    const double idot = double(reader.readSigned(14)) * std::ldexp(1.0, -43) * kSemicircleToDegree;
    const quint64 l2Code = reader.readBits(2);
    const quint64 week = reader.readBits(10);
    const quint64 svAccuracy = reader.readBits(4);
    const quint64 svHealth = reader.readBits(6);
    const double groupDelay = double(reader.readSigned(8)) * std::ldexp(1.0, -31);
    const quint64 iodc = reader.readBits(10);
    const quint64 fitInterval = reader.readBits(1);

    out.setGroup(QStringLiteral("卫星与时间"));
    out.add(QStringLiteral("卫星号"), satelliteLabel(QStringLiteral("J"), int(prn)));
    out.add(QStringLiteral("GPS 周"), QString::number(week));
    out.add(QStringLiteral("toe"), QStringLiteral("%1 s").arg(formatNumber(toe, 0)));
    out.add(QStringLiteral("toc"), QStringLiteral("%1 s").arg(formatNumber(toc, 0)));
    out.add(QStringLiteral("IODE"), QString::number(iode));
    out.add(QStringLiteral("IODC"), QString::number(iodc));
    out.add(QStringLiteral("拟合区间"), fitInterval == 0 ? QStringLiteral("0 (2 小时)") : QStringLiteral("1 (大于 2 小时)"));

    out.setGroup(QStringLiteral("时钟参数"));
    out.add(QStringLiteral("af0"), QStringLiteral("%1 s").arg(formatNumber(af0, 12)));
    out.add(QStringLiteral("af1"), QStringLiteral("%1 s/s").arg(formatNumber(af1, 14)));
    out.add(QStringLiteral("af2"), QStringLiteral("%1 s/s²").arg(formatNumber(af2, 16)));
    out.add(QStringLiteral("群延迟 TGD"), QStringLiteral("%1 s").arg(formatNumber(groupDelay, 12)));

    out.setGroup(QStringLiteral("轨道参数"));
    out.add(QStringLiteral("√A"), QStringLiteral("%1 √m").arg(formatNumber(sqrtSemiMajorAxis, 6)));
    out.add(QStringLiteral("偏心率 e"), formatNumber(eccentricity, 12));
    out.add(QStringLiteral("参考时刻倾角 i0"), degrees(i0));
    out.add(QStringLiteral("升交点经度 Ω0"), degrees(omega0));
    out.add(QStringLiteral("近地点角距 ω"), degrees(omega));
    out.add(QStringLiteral("平近点角 M0"), degrees(m0));
    out.add(QStringLiteral("平均角速度改正 Δn"), QStringLiteral("%1 °/s").arg(formatNumber(deltaN, 12)));
    out.add(QStringLiteral("升交点赤经变化率 ΩDOT"), QStringLiteral("%1 °/s").arg(formatNumber(omegaDot, 12)));
    out.add(QStringLiteral("倾角变化率 IDOT"), QStringLiteral("%1 °/s").arg(formatNumber(idot, 12)));
    out.add(QStringLiteral("Cuc"), QStringLiteral("%1 rad").arg(formatNumber(cuc, 12)));
    out.add(QStringLiteral("Cus"), QStringLiteral("%1 rad").arg(formatNumber(cus, 12)));
    out.add(QStringLiteral("Cic"), QStringLiteral("%1 rad").arg(formatNumber(cic, 12)));
    out.add(QStringLiteral("Cis"), QStringLiteral("%1 rad").arg(formatNumber(cis, 12)));
    out.add(QStringLiteral("Crc"), meters(crc));
    out.add(QStringLiteral("Crs"), meters(crs));

    out.setGroup(QStringLiteral("健康与标志"));
    out.add(QStringLiteral("URA 精度指数"), QString::number(svAccuracy));
    out.add(QStringLiteral("卫星健康"), QString::number(svHealth));
    out.add(QStringLiteral("L2 码型"), QString::number(l2Code));

    return QStringLiteral("PRN %1 周 %2 toe %3").arg(prn).arg(week).arg(formatNumber(toe, 0));
}

/*****************************************************
函数名称：QString decodeGalileoEphemeris(BitReader& reader, FieldCollector& out, bool withSecondFrequency)
入口参数：reader —— 位读取器；out —— 字段收集器；withSecondFrequency —— 是否为 I/NAV（含 E5b）
出口参数：帧摘要文本
函数功能：解码 1045/1046 Galileo 星历消息
*****************************************************/
QString decodeGalileoEphemeris(BitReader& reader, FieldCollector& out, bool withSecondFrequency)
{
    const quint64 prn = reader.readBits(6);
    const quint64 week = reader.readBits(12);
    const quint64 iodnav = reader.readBits(10);
    const quint64 signalInSpaceAccuracy = reader.readBits(8);
    const double idot = double(reader.readSigned(14)) * std::ldexp(1.0, -43) * kSemicircleToDegree;
    const double toc = double(reader.readBits(14)) * 60.0;
    const double af2 = double(reader.readSigned(6)) * std::ldexp(1.0, -59);
    const double af1 = double(reader.readSigned(21)) * std::ldexp(1.0, -46);
    const double af0 = double(reader.readSigned(31)) * std::ldexp(1.0, -34);
    const double crs = double(reader.readSigned(16)) * std::ldexp(1.0, -5);
    const double deltaN = double(reader.readSigned(16)) * std::ldexp(1.0, -43) * kSemicircleToDegree;
    const double m0 = double(reader.readSigned(32)) * std::ldexp(1.0, -31) * kSemicircleToDegree;
    const double cuc = double(reader.readSigned(16)) * std::ldexp(1.0, -29);
    const double eccentricity = double(reader.readBits(32)) * std::ldexp(1.0, -33);
    const double cus = double(reader.readSigned(16)) * std::ldexp(1.0, -29);
    const double sqrtSemiMajorAxis = double(reader.readBits(32)) * std::ldexp(1.0, -19);
    const double toe = double(reader.readBits(14)) * 60.0;
    const double cic = double(reader.readSigned(16)) * std::ldexp(1.0, -29);
    const double omega0 = double(reader.readSigned(32)) * std::ldexp(1.0, -31) * kSemicircleToDegree;
    const double cis = double(reader.readSigned(16)) * std::ldexp(1.0, -29);
    const double i0 = double(reader.readSigned(32)) * std::ldexp(1.0, -31) * kSemicircleToDegree;
    const double crc = double(reader.readSigned(16)) * std::ldexp(1.0, -5);
    const double omega = double(reader.readSigned(32)) * std::ldexp(1.0, -31) * kSemicircleToDegree;
    const double omegaDot = double(reader.readSigned(24)) * std::ldexp(1.0, -43) * kSemicircleToDegree;
    const double bgdE5aE1 = double(reader.readSigned(10)) * std::ldexp(1.0, -32);
    double bgdE5bE1 = 0.0;
    quint64 e5aHealth = 0;
    quint64 e5aValidity = 0;
    quint64 e5bHealth = 0;
    quint64 e5bValidity = 0;
    quint64 e1bHealth = 0;
    quint64 e1bValidity = 0;
    if (withSecondFrequency)
    {
        // I/NAV：E5a/E1 偏差之后依次为 E5b/E1 偏差与 E5b、E1B 的健康标志。
        bgdE5bE1 = double(reader.readSigned(10)) * std::ldexp(1.0, -32);
        e5bHealth = reader.readBits(2);
        e5bValidity = reader.readBits(1);
        e1bHealth = reader.readBits(2);
        e1bValidity = reader.readBits(1);
    }
    else
    {
        // F/NAV：仅携带 E5a 健康标志与数据有效性，之后为保留位。
        e5aHealth = reader.readBits(2);
        e5aValidity = reader.readBits(1);
        reader.skip(7);
    }

    out.setGroup(QStringLiteral("卫星与时间"));
    out.add(QStringLiteral("卫星号"), satelliteLabel(QStringLiteral("E"), int(prn)));
    out.add(QStringLiteral("Galileo 周"), QString::number(week));
    out.add(QStringLiteral("toe"), QStringLiteral("%1 s").arg(formatNumber(toe, 0)));
    out.add(QStringLiteral("toc"), QStringLiteral("%1 s").arg(formatNumber(toc, 0)));
    out.add(QStringLiteral("IODnav"), QString::number(iodnav));
    out.add(QStringLiteral("SISA"), QString::number(signalInSpaceAccuracy));

    out.setGroup(QStringLiteral("时钟参数"));
    out.add(QStringLiteral("af0"), QStringLiteral("%1 s").arg(formatNumber(af0, 12)));
    out.add(QStringLiteral("af1"), QStringLiteral("%1 s/s").arg(formatNumber(af1, 14)));
    out.add(QStringLiteral("af2"), QStringLiteral("%1 s/s²").arg(formatNumber(af2, 16)));
    out.add(QStringLiteral("BGD E5a/E1"), QStringLiteral("%1 s").arg(formatNumber(bgdE5aE1, 14)));
    if (withSecondFrequency)
    {
        out.add(QStringLiteral("BGD E5b/E1"), QStringLiteral("%1 s").arg(formatNumber(bgdE5bE1, 14)));
    }

    out.setGroup(QStringLiteral("轨道参数"));
    out.add(QStringLiteral("√A"), QStringLiteral("%1 √m").arg(formatNumber(sqrtSemiMajorAxis, 6)));
    out.add(QStringLiteral("偏心率 e"), formatNumber(eccentricity, 12));
    out.add(QStringLiteral("参考时刻倾角 i0"), degrees(i0));
    out.add(QStringLiteral("升交点经度 Ω0"), degrees(omega0));
    out.add(QStringLiteral("近地点角距 ω"), degrees(omega));
    out.add(QStringLiteral("平近点角 M0"), degrees(m0));
    out.add(QStringLiteral("平均角速度改正 Δn"), QStringLiteral("%1 °/s").arg(formatNumber(deltaN, 12)));
    out.add(QStringLiteral("升交点赤经变化率 ΩDOT"), QStringLiteral("%1 °/s").arg(formatNumber(omegaDot, 12)));
    out.add(QStringLiteral("倾角变化率 IDOT"), QStringLiteral("%1 °/s").arg(formatNumber(idot, 12)));
    out.add(QStringLiteral("Cuc"), QStringLiteral("%1 rad").arg(formatNumber(cuc, 12)));
    out.add(QStringLiteral("Cus"), QStringLiteral("%1 rad").arg(formatNumber(cus, 12)));
    out.add(QStringLiteral("Cic"), QStringLiteral("%1 rad").arg(formatNumber(cic, 12)));
    out.add(QStringLiteral("Cis"), QStringLiteral("%1 rad").arg(formatNumber(cis, 12)));
    out.add(QStringLiteral("Crc"), meters(crc));
    out.add(QStringLiteral("Crs"), meters(crs));

    out.setGroup(QStringLiteral("信号健康"));
    if (withSecondFrequency)
    {
        out.add(QStringLiteral("E5b 健康"), QString::number(e5bHealth));
        out.add(QStringLiteral("E5b 数据有效性"), flagText(e5bValidity));
        out.add(QStringLiteral("E1B 健康"), QString::number(e1bHealth));
        out.add(QStringLiteral("E1B 数据有效性"), flagText(e1bValidity));
    }
    else
    {
        out.add(QStringLiteral("E5a 健康"), QString::number(e5aHealth));
        out.add(QStringLiteral("E5a 数据有效性"), flagText(e5aValidity));
    }

    return QStringLiteral("PRN %1 周 %2 toe %3").arg(prn).arg(week).arg(formatNumber(toe, 0));
}

/*****************************************************
函数名称：QString decodeGlonassCodePhaseBiases(BitReader& reader, FieldCollector& out)
入口参数：reader —— 位读取器；out —— 字段收集器
出口参数：帧摘要文本
函数功能：解码 1230 GLONASS 码相位偏差消息
*****************************************************/
QString decodeGlonassCodePhaseBiases(BitReader& reader, FieldCollector& out)
{
    const quint64 stationId = reader.readBits(12);
    const quint64 codePhaseBiasIndicator = reader.readBits(1);
    reader.skip(3);
    const quint64 l1CaMask = reader.readBits(1);
    const quint64 l1PMask = reader.readBits(1);
    const quint64 l2CaMask = reader.readBits(1);
    const quint64 l2PMask = reader.readBits(1);

    out.setGroup(QStringLiteral("帧头"));
    out.add(QStringLiteral("基站 ID"), QString::number(stationId));
    out.add(QStringLiteral("码相位偏差指示"), QString::number(codePhaseBiasIndicator));

    out.setGroup(QStringLiteral("码相位偏差"));
    if (l1CaMask != 0)
    {
        out.add(QStringLiteral("L1 C/A 偏差"), meters(double(reader.readSigned(16)) * 0.02));
    }
    if (l1PMask != 0)
    {
        out.add(QStringLiteral("L1 P 偏差"), meters(double(reader.readSigned(16)) * 0.02));
    }
    if (l2CaMask != 0)
    {
        out.add(QStringLiteral("L2 C/A 偏差"), meters(double(reader.readSigned(16)) * 0.02));
    }
    if (l2PMask != 0)
    {
        out.add(QStringLiteral("L2 P 偏差"), meters(double(reader.readSigned(16)) * 0.02));
    }

    return QStringLiteral("%1 项偏差")
        .arg(int(l1CaMask) + int(l1PMask) + int(l2CaMask) + int(l2PMask));
}

/*****************************************************
函数名称：QString decodeMsm(BitReader& reader, FieldCollector& out, int messageNumber)
入口参数：reader —— 位读取器；out —— 字段收集器；messageNumber —— 消息号
出口参数：帧摘要文本
函数功能：解码 1071~1137 MSM 多信号消息的帧头与掩码信息
*****************************************************/
QString decodeMsm(BitReader& reader, FieldCollector& out, int messageNumber)
{
    const int family = messageNumber / 10;
    const int msmType = messageNumber % 10;
    MsmSystem system;
    if (!msmSystemFor(family, system))
    {
        return QString();
    }

    const quint64 stationId = reader.readBits(12);
    QString epochText;
    if (family == 108)
    {
        // GLONASS 使用星期与日内秒作为历元时间。
        const quint64 dayOfWeek = reader.readBits(3);
        const double secondsOfDay = double(reader.readBits(27)) * 0.001;
        epochText = QStringLiteral("第 %1 天 %2 s").arg(dayOfWeek).arg(formatNumber(secondsOfDay, 3));
    }
    else
    {
        const double tow = double(reader.readBits(30)) * 0.001;
        epochText = QStringLiteral("%1 s").arg(formatNumber(tow, 3));
    }
    const quint64 multipleMessage = reader.readBits(1);
    const quint64 iods = reader.readBits(3);
    const quint64 reserved = reader.readBits(7);
    const quint64 clockSteering = reader.readBits(2);
    const quint64 externalClock = reader.readBits(2);
    const quint64 smoothingType = reader.readBits(1);
    const quint64 smoothingInterval = reader.readBits(3);

    out.setGroup(QStringLiteral("MSM 帧头"));
    out.add(QStringLiteral("星座"), system.name);
    out.add(QStringLiteral("基站 ID"), QString::number(stationId));
    out.add(QStringLiteral("历元时间"), epochText);
    out.add(QStringLiteral("多消息标志"), flagText(multipleMessage));
    out.syncFlag = int(multipleMessage);
    out.add(QStringLiteral("IODS"), QString::number(iods));
    out.add(QStringLiteral("保留位"), QString::number(reserved));
    out.add(QStringLiteral("时钟转向指示"), QString::number(clockSteering));
    out.add(QStringLiteral("外部时钟指示"), QString::number(externalClock));
    out.add(QStringLiteral("平滑标志"), QString::number(smoothingType));
    out.add(QStringLiteral("平滑间隔（索引）"), QString::number(smoothingInterval));

    // 卫星掩码：位号即卫星标识（QZSS/SBAS 需加偏移）。
    QList<int> satellites;
    for (int bit = 1; bit <= 64; ++bit)
    {
        if (reader.readBits(1) != 0)
        {
            satellites.append(bit + system.prnOffset);
        }
    }

    // 信号掩码：位号对应星座信号标识。
    QList<int> signalIds;
    for (int bit = 1; bit <= 32; ++bit)
    {
        if (reader.readBits(1) != 0)
        {
            signalIds.append(bit);
        }
    }

    const int satelliteCount = satellites.size();
    const int signalCount = signalIds.size();

    // 单元掩码：按卫星顺序排列，每颗卫星 signalCount 位。
    QList<bool> cells;
    cells.reserve(satelliteCount * signalCount);
    int cellCount = 0;
    for (int index = 0; index < satelliteCount * signalCount; ++index)
    {
        const bool present = reader.readBits(1) != 0;
        cells.append(present);
        if (present)
        {
            ++cellCount;
        }
    }

    out.setGroup(QStringLiteral("卫星"));
    out.add(QStringLiteral("卫星数"), QString::number(satelliteCount));
    QStringList satelliteNames;
    for (int index = 0; index < satelliteCount; ++index)
    {
        satelliteNames.append(satelliteLabel(system.prefix, satellites.at(index)));
    }
    out.add(QStringLiteral("卫星列表"), satelliteNames.join(QLatin1Char(' ')));

    out.setGroup(QStringLiteral("信号"));
    out.add(QStringLiteral("信号数"), QString::number(signalCount));
    QStringList signalNames;
    for (int index = 0; index < signalCount; ++index)
    {
        const int signalId = signalIds.at(index);
        const char* name = system.signalTable[signalId];
        signalNames.append(name != nullptr && name[0] != '\0'
                               ? QStringLiteral("L%1").arg(QString::fromLatin1(name))
                               : QStringLiteral("ID %1").arg(signalId));
    }
    out.add(QStringLiteral("信号列表"), signalNames.join(QStringLiteral(", ")));

    out.setGroup(QStringLiteral("单元掩码"));
    out.add(QStringLiteral("单元数"), QString::number(cellCount));
    for (int satelliteIndex = 0; satelliteIndex < satelliteCount; ++satelliteIndex)
    {
        QStringList presentSignals;
        for (int signalIndex = 0; signalIndex < signalCount; ++signalIndex)
        {
            if (cells.at(satelliteIndex * signalCount + signalIndex))
            {
                presentSignals.append(signalNames.at(signalIndex));
            }
        }
        out.add(satelliteNames.at(satelliteIndex),
                presentSignals.isEmpty() ? QStringLiteral("无") : presentSignals.join(QStringLiteral(" ")));
    }

    out.setGroup(QStringLiteral("观测数据"));
    out.add(QStringLiteral("观测内容"), QStringLiteral("MSM%1（%2）").arg(msmType).arg(msmTypeText(msmType)));
    out.add(QStringLiteral("观测数据字节数"), QString::number(reader.remainingBits() / 8));
    out.add(QStringLiteral("说明"), QStringLiteral("仅解析 MSM 帧头与掩码，未展开逐卫星观测量"));

    return QStringLiteral("%1 星 / %2 信号 / %3 单元")
        .arg(satelliteCount)
        .arg(signalCount)
        .arg(cellCount);
}
}  // namespace

/*****************************************************
函数名称：quint32 Rtcm3Parser::crc24q(const QByteArray& data)
入口参数：data —— 参与校验的字节
出口参数：24 位校验值
函数功能：按 CRC-24Q（多项式 0x1864CFB）计算校验值
*****************************************************/
quint32 Rtcm3Parser::crc24q(const QByteArray& data)
{
    quint32 crc = 0;
    for (int index = 0; index < data.size(); ++index)
    {
        crc ^= quint32(static_cast<quint8>(data.at(index))) << 16;
        for (int bit = 0; bit < 8; ++bit)
        {
            crc <<= 1;
            if ((crc & 0x1000000) != 0)
            {
                crc ^= 0x1864CFB;
            }
        }
    }
    return crc & 0xFFFFFF;
}

/*****************************************************
函数名称：bool Rtcm3Parser::isMsmMessage(int messageNumber)
入口参数：messageNumber —— 消息号
出口参数：是否为 MSM 消息
函数功能：判断消息号是否落在 1071~1137 的多信号消息区间
*****************************************************/
bool Rtcm3Parser::isMsmMessage(int messageNumber)
{
    const int family = messageNumber / 10;
    const int msmType = messageNumber % 10;
    return family >= 107 && family <= 113 && msmType >= 1 && msmType <= 7;
}

/*****************************************************
函数名称：QString Rtcm3Parser::messageName(int messageNumber)
入口参数：messageNumber —— 消息号
出口参数：消息中文名称
函数功能：返回消息号对应的中文名称
*****************************************************/
QString Rtcm3Parser::messageName(int messageNumber)
{
    static const QHash<int, QString> names = {
        {1001, QStringLiteral("GPS L1 观测量")},
        {1002, QStringLiteral("GPS L1 扩展观测量")},
        {1003, QStringLiteral("GPS L1/L2 观测量")},
        {1004, QStringLiteral("GPS L1/L2 扩展观测量")},
        {1005, QStringLiteral("基站坐标 (ECEF)")},
        {1006, QStringLiteral("基站坐标 + 天线高")},
        {1007, QStringLiteral("天线描述")},
        {1008, QStringLiteral("天线描述 + 序列号")},
        {1009, QStringLiteral("GLONASS L1 观测量")},
        {1010, QStringLiteral("GLONASS L1 扩展观测量")},
        {1011, QStringLiteral("GLONASS L1/L2 观测量")},
        {1012, QStringLiteral("GLONASS L1/L2 扩展观测量")},
        {1013, QStringLiteral("系统参数")},
        {1019, QStringLiteral("GPS 星历")},
        {1020, QStringLiteral("GLONASS 星历")},
        {1029, QStringLiteral("Unicode 文本")},
        {1033, QStringLiteral("接收机与天线描述")},
        {1230, QStringLiteral("GLONASS 码相位偏差")},
        {1041, QStringLiteral("NavIC 星历")},
        {1042, QStringLiteral("北斗星历")},
        {1044, QStringLiteral("QZSS 星历")},
        {1045, QStringLiteral("Galileo F/NAV 星历")},
        {1046, QStringLiteral("Galileo I/NAV 星历")}};

    const auto iterator = names.constFind(messageNumber);
    if (iterator != names.constEnd())
    {
        return iterator.value();
    }
    if (messageNumber == 0)
    {
        // 消息号 0 不属于任何 RTCM3 消息，通常是噪声或截断数据被误判为帧。
        return QStringLiteral("无效帧");
    }
    if (isMsmMessage(messageNumber))
    {
        MsmSystem system;
        const int family = messageNumber / 10;
        const int msmType = messageNumber % 10;
        if (msmSystemFor(family, system))
        {
            return QStringLiteral("%1 MSM%2").arg(system.name).arg(msmType);
        }
    }
    if (messageNumber >= 1057 && messageNumber <= 1068)
    {
        return QStringLiteral("GPS/GLONASS SSR 改正数");
    }
    if (messageNumber >= 1240 && messageNumber <= 1263)
    {
        return QStringLiteral("Galileo/QZSS/北斗 SSR 改正数");
    }
    if (messageNumber == 1264)
    {
        return QStringLiteral("SSR 电离层 VTEC");
    }
    if (messageNumber >= 4001 && messageNumber <= 4095)
    {
        return QStringLiteral("厂商自定义消息");
    }
    return QStringLiteral("未知消息 %1").arg(messageNumber);
}

/*****************************************************
函数名称：QString Rtcm3Parser::shortName(int messageNumber)
入口参数：messageNumber —— 消息号
出口参数：短名称
函数功能：生成帧列表使用的短名称
*****************************************************/
QString Rtcm3Parser::shortName(int messageNumber)
{
    if (isMsmMessage(messageNumber))
    {
        MsmSystem system;
        const int family = messageNumber / 10;
        const int msmType = messageNumber % 10;
        if (msmSystemFor(family, system))
        {
            return QStringLiteral("%1 MSM%2").arg(system.prefix, QString::number(msmType));
        }
    }
    return messageName(messageNumber);
}

/*****************************************************
函数名称：QList<Rtcm3Frame> Rtcm3Parser::append(const QByteArray& data)
入口参数：data —— 新到达的原始字节
出口参数：本次解析出的完整帧列表
函数功能：按 0xD3 帧头与长度域做流同步，校验 CRC 并解码载荷
*****************************************************/
QList<Rtcm3Frame> Rtcm3Parser::append(const QByteArray& data)
{
    QList<Rtcm3Frame> frames;
    if (!data.isEmpty())
    {
        _buffer.append(data);
    }

    while (!_buffer.isEmpty())
    {
        // 定位帧头，之前的字节按噪声丢弃。
        int startIndex = -1;
        for (int index = 0; index < _buffer.size(); ++index)
        {
            if (static_cast<quint8>(_buffer.at(index)) == kPreamble)
            {
                startIndex = index;
                break;
            }
        }
        if (startIndex < 0)
        {
            _droppedByteCount += quint64(_buffer.size());
            _buffer.clear();
            break;
        }
        if (startIndex > 0)
        {
            _droppedByteCount += quint64(startIndex);
            _buffer.remove(0, startIndex);
        }

        // 至少需要 3 字节才能读出长度域。
        if (_buffer.size() < 3)
        {
            break;
        }

        // 帧头后 6 位为保留位，必须为 0，用于排除数据中的伪帧头。
        const bool reservedBitsValid = (static_cast<quint8>(_buffer.at(1)) & 0xFC) == 0;
        const int payloadLength =
            ((static_cast<quint8>(_buffer.at(1)) & 0x03) << 8) | static_cast<quint8>(_buffer.at(2));
        if (!reservedBitsValid || payloadLength < 2 || payloadLength > kMaximumPayloadLength)
        {
            // 保留位或长度非法，丢弃该帧头字节后继续同步。
            ++_droppedByteCount;
            _buffer.remove(0, 1);
            continue;
        }

        const int frameLength = payloadLength + 6;
        if (_buffer.size() < frameLength)
        {
            break;  // 帧未接收完整，等待后续字节
        }

        Rtcm3Frame frame;
        frame.timestamp = QDateTime::currentDateTime();
        frame.raw = _buffer.left(frameLength);
        frame.payloadLength = payloadLength;

        const QByteArray payload = frame.raw.mid(3, payloadLength);
        frame.messageNumber = (static_cast<quint8>(payload.at(0)) << 4) |
                              (static_cast<quint8>(payload.at(1)) >> 4);
        frame.messageName = messageName(frame.messageNumber);

        const QByteArray checked = frame.raw.left(frameLength - 3);
        frame.crcComputed = crc24q(checked);
        frame.crcReceived = (quint32(static_cast<quint8>(frame.raw.at(frameLength - 3))) << 16) |
                            (quint32(static_cast<quint8>(frame.raw.at(frameLength - 2))) << 8) |
                            quint32(static_cast<quint8>(frame.raw.at(frameLength - 1)));
        frame.crcValid = frame.crcComputed == frame.crcReceived;

        ++_frameCount;
        if (frame.crcValid)
        {
            _buffer.remove(0, frameLength);
            decodePayload(frame);
        }
        else
        {
            // CRC 失败可能是伪帧头，只丢弃帧头字节重新同步，避免吞掉其后的有效帧。
            ++_crcErrorCount;
            ++_droppedByteCount;
            _buffer.remove(0, 1);
            frame.summary = QStringLiteral("CRC 校验失败");
        }
        frames.append(frame);
    }
    return frames;
}

/*****************************************************
函数名称：void Rtcm3Parser::reset()
入口参数：无
出口参数：无
函数功能：清空内部缓冲与统计计数
*****************************************************/
void Rtcm3Parser::reset()
{
    _buffer.clear();
    _frameCount = 0;
    _crcErrorCount = 0;
    _droppedByteCount = 0;
}

/*****************************************************
函数名称：quint64 Rtcm3Parser::frameCount() const
入口参数：无
出口参数：累计解析帧数
函数功能：获取累计解析帧数
*****************************************************/
quint64 Rtcm3Parser::frameCount() const
{
    return _frameCount;
}

/*****************************************************
函数名称：quint64 Rtcm3Parser::crcErrorCount() const
入口参数：无
出口参数：累计CRC错误帧数
函数功能：获取累计CRC错误帧数
*****************************************************/
quint64 Rtcm3Parser::crcErrorCount() const
{
    return _crcErrorCount;
}

/*****************************************************
函数名称：quint64 Rtcm3Parser::droppedByteCount() const
入口参数：无
出口参数：累计丢弃的噪声字节数
函数功能：获取累计丢弃的噪声字节数
*****************************************************/
quint64 Rtcm3Parser::droppedByteCount() const
{
    return _droppedByteCount;
}

/*****************************************************
函数名称：void Rtcm3Parser::decodePayload(Rtcm3Frame& frame)
入口参数：frame —— 待解码的完整帧
出口参数：无
函数功能：按消息号分派载荷解码并填充字段列表与摘要
*****************************************************/
void Rtcm3Parser::decodePayload(Rtcm3Frame& frame)
{
    BitReader reader(frame.raw.mid(3, frame.payloadLength));
    reader.skip(12);  // 跳过已读取的消息号
    FieldCollector out{&frame.fields, QString()};

    switch (frame.messageNumber)
    {
    case 1001:
    case 1002:
    case 1003:
    case 1004:
        frame.summary = decodeGpsObservables(reader, out, frame.messageNumber);
        break;
    case 1005:
        frame.summary = decodeStationCoordinates(reader, out, false);
        break;
    case 1006:
        frame.summary = decodeStationCoordinates(reader, out, true);
        break;
    case 1007:
        frame.summary = decodeAntennaDescriptor(reader, out, false);
        break;
    case 1008:
        frame.summary = decodeAntennaDescriptor(reader, out, true);
        break;
    case 1009:
    case 1010:
    case 1011:
    case 1012:
        frame.summary = decodeGlonassObservables(reader, out, frame.messageNumber);
        break;
    case 1013:
        frame.summary = decodeSystemParameters(reader, out);
        break;
    case 1019:
        frame.summary = decodeGpsEphemeris(reader, out);
        break;
    case 1020:
        frame.summary = decodeGlonassEphemeris(reader, out);
        break;
    case 1029:
        frame.summary = decodeUnicodeText(reader, out);
        break;
    case 1033:
        frame.summary = decodeReceiverDescriptor(reader, out);
        break;
    case 1042:
        frame.summary = decodeBeidouEphemeris(reader, out);
        break;
    case 1044:
        frame.summary = decodeQzssEphemeris(reader, out);
        break;
    case 1045:
        frame.summary = decodeGalileoEphemeris(reader, out, false);
        break;
    case 1046:
        frame.summary = decodeGalileoEphemeris(reader, out, true);
        break;
    case 1230:
        frame.summary = decodeGlonassCodePhaseBiases(reader, out);
        break;
    default:
        if (isMsmMessage(frame.messageNumber))
        {
            frame.summary = decodeMsm(reader, out, frame.messageNumber);
        }
        break;
    }

    frame.syncFlag = out.syncFlag;

    if (frame.fields.isEmpty())
    {
        out.setGroup(QStringLiteral("载荷"));
        out.add(QStringLiteral("字段解码"), QStringLiteral("该消息暂未展开字段解码"));
        if (frame.summary.isEmpty())
        {
            frame.summary = QStringLiteral("载荷 %1 字节").arg(frame.payloadLength);
        }
    }
}
