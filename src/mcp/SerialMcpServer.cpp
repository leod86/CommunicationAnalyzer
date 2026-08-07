#include "mcp/SerialMcpServer.h"

#include "communication/SerialController.h"

#include <QDateTime>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSerialPort>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>

namespace
{
constexpr quint16 kMcpPort = 8765;
constexpr qsizetype kMaximumHttpHeaderBytes = 32 * 1024;
constexpr qsizetype kMaximumHttpBodyBytes = 1024 * 1024;
constexpr qsizetype kMaximumSendBytes = 64 * 1024;
constexpr qsizetype kMaximumStoredTrafficRecordBytes = 256 * 1024;
constexpr qsizetype kMaximumTrafficBytes = 4 * 1024 * 1024;
constexpr int kMaximumTrafficRecords = 2000;
constexpr int kMaximumReadRecords = 200;
constexpr qsizetype kMaximumToolResponseBytes = 1024 * 1024;
constexpr int kMaximumToolCallsPerWindow = 30;
constexpr qint64 kToolRateWindowMilliseconds = 10 * 1000;
const QByteArray kLatestProtocolVersion("2026-07-28");

/*****************************************************
函数名称：QByteArray httpReasonPhrase(int status)
入口参数：status为HTTP状态码
出口参数：HTTP原因短语
函数功能：返回本地MCP响应使用的标准状态说明
*****************************************************/
QByteArray httpReasonPhrase(int status)
{
    switch (status)
    {
    case 200:
        return QByteArrayLiteral("OK");
    case 202:
        return QByteArrayLiteral("Accepted");
    case 400:
        return QByteArrayLiteral("Bad Request");
    case 403:
        return QByteArrayLiteral("Forbidden");
    case 404:
        return QByteArrayLiteral("Not Found");
    case 405:
        return QByteArrayLiteral("Method Not Allowed");
    case 413:
        return QByteArrayLiteral("Payload Too Large");
    default:
        return QByteArrayLiteral("Error");
    }
}

/*****************************************************
函数名称：QJsonObject textContent(const QString& text)
入口参数：text为工具返回文本
出口参数：MCP文本内容对象
函数功能：构造一个MCP TextContent对象
*****************************************************/
QJsonObject textContent(const QString& text)
{
    return QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                       {QStringLiteral("text"), text}};
}

/*****************************************************
函数名称：QJsonObject toolAnnotation(bool readOnly, bool destructive, bool idempotent)
入口参数：readOnly为只读标志，destructive为破坏性标志，idempotent为幂等标志
出口参数：MCP工具注解对象
函数功能：构造供Codex判断读取和控制风险的工具注解
*****************************************************/
QJsonObject toolAnnotation(bool readOnly, bool destructive, bool idempotent)
{
    return QJsonObject{{QStringLiteral("readOnlyHint"), readOnly},
                       {QStringLiteral("destructiveHint"), destructive},
                       {QStringLiteral("idempotentHint"), idempotent},
                       {QStringLiteral("openWorldHint"), false}};
}
}

/*****************************************************
函数名称：SerialMcpServer::SerialMcpServer(SerialController* serialController, QObject* parent)
入口参数：serialController为共享串口控制器，parent为父对象
出口参数：无
函数功能：创建 MCP 服务并订阅串口原始收发信号
*****************************************************/
SerialMcpServer::SerialMcpServer(SerialController* serialController, QObject* parent)
    : QObject(parent)
    , _serialController(serialController)
    , _server(new QTcpServer(this))
{
    // 接收串口控制器发布的原始收发数据，供 MCP 使用独立游标读取。
    connect(_serialController, &SerialController::dataReceived,
            this, &SerialMcpServer::handleReceivedData);
    connect(_serialController, &SerialController::dataSent,
            this, &SerialMcpServer::handleSentData);
    connect(_server, &QTcpServer::newConnection,
            this, &SerialMcpServer::handleNewConnection);
}

/*****************************************************
函数名称：SerialMcpServer::~SerialMcpServer()
入口参数：无
出口参数：无
函数功能：停止 MCP 服务并释放本地 HTTP 连接
*****************************************************/
SerialMcpServer::~SerialMcpServer()
{
    // 父对象析构会释放套接字，此处先停止监听避免继续接收新请求。
    _server->close();
}

/*****************************************************
函数名称：bool SerialMcpServer::setEnabled(bool enabled)
入口参数：enabled为目标启用状态
出口参数：服务是否达到目标状态
函数功能：在本地回环地址启动或停止 Streamable HTTP 服务
*****************************************************/
bool SerialMcpServer::setEnabled(bool enabled)
{
    if (!enabled)
    {
        _enabled = false;
        _server->close();
        const QList<QTcpSocket*> sockets = _socketBuffers.keys();
        _socketBuffers.clear();
        for (QTcpSocket* socket : sockets)
        {
            socket->disconnectFromHost();
        }
        _trafficRecords.clear();
        _trafficBytes = 0;
        emit stateChanged(false, QStringLiteral("MCP 已停止"));
        return true;
    }

    if (_enabled)
    {
        return true;
    }

    if (!_server->listen(QHostAddress::LocalHost, kMcpPort))
    {
        emit stateChanged(false, QStringLiteral("MCP 启动失败：%1").arg(_server->errorString()));
        return false;
    }

    _enabled = true;
    _trafficRecords.clear();
    _trafficBytes = 0;
    _nextTrafficId = 1;
    _toolInvocationTimes.clear();
    _toolRateTimer.invalidate();
    emit stateChanged(true, endpoint());
    return true;
}

/*****************************************************
函数名称：bool SerialMcpServer::isEnabled() const
入口参数：无
出口参数：MCP 服务是否已启用
函数功能：返回当前 MCP 服务状态
*****************************************************/
bool SerialMcpServer::isEnabled() const
{
    return _enabled;
}

/*****************************************************
函数名称：QString SerialMcpServer::endpoint() const
入口参数：无
出口参数：MCP HTTP 地址
函数功能：返回供 Codex 配置使用的本地 MCP 地址
*****************************************************/
QString SerialMcpServer::endpoint() const
{
    return QStringLiteral("http://127.0.0.1:%1/mcp").arg(kMcpPort);
}

/*****************************************************
函数名称：void SerialMcpServer::handleNewConnection()
入口参数：无
出口参数：无
函数功能：为新连接安装收发和断开处理
*****************************************************/
void SerialMcpServer::handleNewConnection()
{
    while (_server->hasPendingConnections())
    {
        QTcpSocket* socket = _server->nextPendingConnection();
        _socketBuffers.insert(socket, QByteArray());
        // 每个 HTTP 请求处理完成后主动关闭连接，避免维护无状态连接会话。
        connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
            processSocket(socket);
        });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket]() {
            _socketBuffers.remove(socket);
            socket->deleteLater();
        });
    }
}

/*****************************************************
函数名称：void SerialMcpServer::handleReceivedData(const QByteArray& data)
入口参数：data为串口接收的原始字节
出口参数：无
函数功能：保存一条串口接收记录供 MCP 读取
*****************************************************/
void SerialMcpServer::handleReceivedData(const QByteArray& data)
{
    if (_enabled)
    {
        appendTrafficRecord(true, data);
    }
}

/*****************************************************
函数名称：void SerialMcpServer::handleSentData(const QByteArray& data)
入口参数：data为串口发送的原始字节
出口参数：无
函数功能：保存一条串口发送记录供 MCP 读取
*****************************************************/
void SerialMcpServer::handleSentData(const QByteArray& data)
{
    if (_enabled)
    {
        appendTrafficRecord(false, data);
    }
}

/*****************************************************
函数名称：void SerialMcpServer::processSocket(QTcpSocket* socket)
入口参数：socket为本地 MCP HTTP 套接字
出口参数：无
函数功能：累积并处理套接字中的一个 HTTP 请求
*****************************************************/
void SerialMcpServer::processSocket(QTcpSocket* socket)
{
    if (!_enabled || !_socketBuffers.contains(socket))
    {
        socket->disconnectFromHost();
        return;
    }

    QByteArray& buffer = _socketBuffers[socket];
    buffer.append(socket->readAll());
    if (buffer.size() > kMaximumHttpHeaderBytes + kMaximumHttpBodyBytes)
    {
        socket->write(makeHttpResponse(413, QByteArrayLiteral("text/plain; charset=utf-8"),
                                       QByteArrayLiteral("request too large")));
        socket->disconnectFromHost();
        return;
    }

    HttpRequest request;
    const RequestParseResult result = parseHttpRequest(buffer, request);
    if (result == RequestParseResult::Incomplete)
    {
        return;
    }
    if (result == RequestParseResult::Invalid)
    {
        socket->write(makeHttpResponse(400, QByteArrayLiteral("text/plain; charset=utf-8"),
                                       QByteArrayLiteral("invalid HTTP request")));
        socket->disconnectFromHost();
        return;
    }

    dispatchRequest(socket, request);
}

/*****************************************************
函数名称：SerialMcpServer::RequestParseResult SerialMcpServer::parseHttpRequest(QByteArray& buffer, HttpRequest& request) const
入口参数：buffer为HTTP缓存，request为解析结果
出口参数：请求解析状态
函数功能：解析请求行、请求头和Content-Length指定的请求体
*****************************************************/
SerialMcpServer::RequestParseResult SerialMcpServer::parseHttpRequest(
    QByteArray& buffer, HttpRequest& request) const
{
    const qsizetype headerEnd = buffer.indexOf(QByteArrayLiteral("\r\n\r\n"));
    if (headerEnd < 0)
    {
        return buffer.size() > kMaximumHttpHeaderBytes
            ? RequestParseResult::Invalid
            : RequestParseResult::Incomplete;
    }
    if (headerEnd > kMaximumHttpHeaderBytes)
    {
        return RequestParseResult::Invalid;
    }

    const QList<QByteArray> lines = buffer.left(headerEnd).split('\n');
    if (lines.isEmpty())
    {
        return RequestParseResult::Invalid;
    }

    const QList<QByteArray> requestLine = lines.first().trimmed().split(' ');
    if (requestLine.size() != 3 || requestLine.at(2) != QByteArrayLiteral("HTTP/1.1"))
    {
        return RequestParseResult::Invalid;
    }
    request.method = requestLine.at(0).toUpper();
    request.target = requestLine.at(1);

    request.headers.clear();
    for (qsizetype index = 1; index < lines.size(); ++index)
    {
        const QByteArray line = lines.at(index).trimmed();
        const qsizetype separator = line.indexOf(':');
        if (separator <= 0)
        {
            return RequestParseResult::Invalid;
        }
        const QByteArray name = line.left(separator).trimmed().toLower();
        const QByteArray value = line.mid(separator + 1).trimmed();
        request.headers.insert(name, value);
    }

    bool contentLengthValid = true;
    const QByteArray contentLengthHeader = request.headers.value(QByteArrayLiteral("content-length"));
    const qint64 contentLength = contentLengthHeader.isEmpty()
        ? 0
        : contentLengthHeader.toLongLong(&contentLengthValid);
    if (!contentLengthValid || contentLength < 0 || contentLength > kMaximumHttpBodyBytes)
    {
        return RequestParseResult::Invalid;
    }

    const qsizetype totalLength = headerEnd + 4 + static_cast<qsizetype>(contentLength);
    if (buffer.size() < totalLength)
    {
        return RequestParseResult::Incomplete;
    }

    request.body = buffer.mid(headerEnd + 4, contentLength);
    buffer.remove(0, totalLength);
    return RequestParseResult::Complete;
}

/*****************************************************
函数名称：void SerialMcpServer::dispatchRequest(QTcpSocket* socket, const HttpRequest& request)
入口参数：socket为响应套接字，request为HTTP请求
出口参数：无
函数功能：校验HTTP边界并分发MCP JSON-RPC调用
*****************************************************/
void SerialMcpServer::dispatchRequest(QTcpSocket* socket, const HttpRequest& request)
{
    const QUrl requestUrl = QUrl::fromEncoded(request.target);
    const QByteArray path = requestUrl.path(QUrl::FullyDecoded).toUtf8();
    if (path != QByteArrayLiteral("/mcp"))
    {
        socket->write(makeHttpResponse(404, QByteArrayLiteral("text/plain; charset=utf-8"),
                                       QByteArrayLiteral("MCP endpoint not found")));
        socket->disconnectFromHost();
        return;
    }

    if (!isOriginAllowed(request.headers.value(QByteArrayLiteral("origin"))))
    {
        socket->write(makeHttpResponse(403, QByteArrayLiteral("text/plain; charset=utf-8"),
                                       QByteArrayLiteral("origin is not allowed")));
        socket->disconnectFromHost();
        return;
    }

    if (request.method != QByteArrayLiteral("POST"))
    {
        socket->write(makeHttpResponse(405, QByteArrayLiteral("text/plain; charset=utf-8"),
                                       QByteArrayLiteral("only POST is supported")));
        socket->disconnectFromHost();
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(request.body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        socket->write(makeHttpResponse(400, QByteArrayLiteral("text/plain; charset=utf-8"),
                                       QByteArrayLiteral("invalid JSON-RPC request")));
        socket->disconnectFromHost();
        return;
    }

    const QJsonObject rpc = document.object();
    const QByteArray protocolHeader = request.headers.value(QByteArrayLiteral("mcp-protocol-version"));
    const QByteArray protocolVersion = protocolHeader.isEmpty()
        ? QByteArrayLiteral("2025-03-26")
        : protocolHeader;
    if (protocolVersion != QByteArrayLiteral("2025-03-26")
        && protocolVersion != QByteArrayLiteral("2025-06-18")
        && protocolVersion != QByteArrayLiteral("2025-11-25")
        && protocolVersion != kLatestProtocolVersion)
    {
        socket->write(makeHttpResponse(400, QByteArrayLiteral("text/plain; charset=utf-8"),
                                       QByteArrayLiteral("unsupported MCP protocol version")));
        socket->disconnectFromHost();
        return;
    }

    const QString method = rpc.value(QStringLiteral("method")).toString();
    const QByteArray methodHeader = request.headers.value(QByteArrayLiteral("mcp-method"));
    if ((!methodHeader.isEmpty() && methodHeader != method.toUtf8())
        || (protocolVersion == kLatestProtocolVersion && methodHeader.isEmpty()))
    {
        socket->write(makeHttpResponse(400, QByteArrayLiteral("text/plain; charset=utf-8"),
                                       QByteArrayLiteral("Mcp-Method does not match the JSON-RPC method")));
        socket->disconnectFromHost();
        return;
    }
    if (protocolVersion == kLatestProtocolVersion)
    {
        const QJsonObject params = rpc.value(QStringLiteral("params")).toObject();
        const QJsonObject metadata = params.value(QStringLiteral("_meta")).toObject();
        const QString bodyVersion = metadata.value(QStringLiteral("io.modelcontextprotocol/protocolVersion"))
                                        .toString();
        if (bodyVersion != QString::fromLatin1(protocolVersion))
        {
            socket->write(makeHttpResponse(400, QByteArrayLiteral("text/plain; charset=utf-8"),
                                           QByteArrayLiteral("protocol metadata does not match the header")));
            socket->disconnectFromHost();
            return;
        }
        if (method == QStringLiteral("tools/call"))
        {
            const QByteArray nameHeader = request.headers.value(QByteArrayLiteral("mcp-name"));
            const QByteArray toolName = params.value(QStringLiteral("name")).toString().toUtf8();
            if (nameHeader != toolName)
            {
                socket->write(makeHttpResponse(400, QByteArrayLiteral("text/plain; charset=utf-8"),
                                               QByteArrayLiteral("Mcp-Name does not match the tool name")));
                socket->disconnectFromHost();
                return;
            }
        }
    }

    const bool isNotification = !rpc.contains(QStringLiteral("id"));
    int httpStatus = 200;
    const QJsonObject response = dispatchRpc(rpc, protocolVersion, httpStatus);
    if (isNotification)
    {
        socket->write(makeHttpResponse(202, QByteArray(), QByteArray()));
        socket->disconnectFromHost();
        return;
    }

    const QByteArray responseBody = QJsonDocument(response).toJson(QJsonDocument::Compact);
    socket->write(makeHttpResponse(httpStatus, QByteArrayLiteral("application/json; charset=utf-8"), responseBody));
    socket->disconnectFromHost();
}

/*****************************************************
函数名称：QJsonObject SerialMcpServer::dispatchRpc(const QJsonObject& rpc, const QByteArray& protocolVersion, int& httpStatus)
入口参数：rpc为JSON-RPC对象，protocolVersion为协商版本，httpStatus为HTTP状态输出
出口参数：JSON-RPC响应对象
函数功能：处理初始化、工具发现、工具调用和心跳请求
*****************************************************/
QJsonObject SerialMcpServer::dispatchRpc(const QJsonObject& rpc, const QByteArray& protocolVersion, int& httpStatus)
{
    const QJsonValue id = rpc.value(QStringLiteral("id"));
    if (rpc.value(QStringLiteral("jsonrpc")).toString() != QStringLiteral("2.0"))
    {
        return makeRpcError(id, -32600, QStringLiteral("Invalid JSON-RPC request"));
    }

    const QString method = rpc.value(QStringLiteral("method")).toString();
    if (method.isEmpty())
    {
        return makeRpcError(id, -32600, QStringLiteral("JSON-RPC method is required"));
    }

    if (method == QStringLiteral("initialize"))
    {
        const QJsonObject params = rpc.value(QStringLiteral("params")).toObject();
        const QString requestedVersion = params.value(QStringLiteral("protocolVersion")).toString();
        const QString selectedVersion = requestedVersion == QString::fromLatin1(kLatestProtocolVersion)
            || requestedVersion == QStringLiteral("2025-03-26")
            || requestedVersion == QStringLiteral("2025-06-18")
            || requestedVersion == QStringLiteral("2025-11-25")
            ? requestedVersion
            : QString::fromLatin1(kLatestProtocolVersion);

        const QJsonObject result{
            {QStringLiteral("protocolVersion"), selectedVersion},
            {QStringLiteral("capabilities"), QJsonObject{{QStringLiteral("tools"), QJsonObject{}}}},
            {QStringLiteral("serverInfo"), QJsonObject{
                {QStringLiteral("name"), QStringLiteral("communication-analyzer-serial")},
                {QStringLiteral("title"), QStringLiteral("CommunicationAnalyzer 串口")},
                {QStringLiteral("version"), QStringLiteral("1.0.0")}}},
            {QStringLiteral("instructions"),
             QStringLiteral("仅操作当前 CommunicationAnalyzer 共享的串口。先调用 serial_get_status；发送数据前确认端口、编码和字节内容。serial_read 使用 after_id 逐页读取收发记录，服务仅监听 127.0.0.1。")}};
        return QJsonObject{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                           {QStringLiteral("id"), id},
                           {QStringLiteral("result"), result}};
    }
    if (method == QStringLiteral("notifications/initialized"))
    {
        return QJsonObject();
    }
    if (method == QStringLiteral("ping"))
    {
        return QJsonObject{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                           {QStringLiteral("id"), id},
                           {QStringLiteral("result"), QJsonObject{}}};
    }
    if (method == QStringLiteral("tools/list"))
    {
        return QJsonObject{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                           {QStringLiteral("id"), id},
                           {QStringLiteral("result"), QJsonObject{
                               {QStringLiteral("tools"), toolDefinitions()}}}};
    }
    if (method == QStringLiteral("tools/call"))
    {
        const QJsonObject params = rpc.value(QStringLiteral("params")).toObject();
        const QString toolName = params.value(QStringLiteral("name")).toString();
        const QJsonObject arguments = params.value(QStringLiteral("arguments")).toObject();
        const QStringList supportedTools{
            QStringLiteral("serial_list_ports"),
            QStringLiteral("serial_get_status"),
            QStringLiteral("serial_read"),
            QStringLiteral("serial_open"),
            QStringLiteral("serial_close"),
            QStringLiteral("serial_send")};
        if (!supportedTools.contains(toolName))
        {
            httpStatus = protocolVersion == kLatestProtocolVersion ? 404 : 200;
            return makeRpcError(id, -32601, QStringLiteral("Unknown tool: %1").arg(toolName));
        }

        return QJsonObject{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                           {QStringLiteral("id"), id},
                           {QStringLiteral("result"), handleToolCall(arguments, toolName)}};
    }

    httpStatus = protocolVersion == kLatestProtocolVersion ? 404 : 200;
    return makeRpcError(id, -32601, QStringLiteral("Method not found: %1").arg(method));
}

/*****************************************************
函数名称：QJsonObject SerialMcpServer::handleToolCall(const QJsonObject& arguments, const QString& toolName)
入口参数：arguments为工具参数，toolName为工具名称
出口参数：MCP工具结果
函数功能：执行串口列表、状态、记录读取和串口控制工具
*****************************************************/
QJsonObject SerialMcpServer::handleToolCall(const QJsonObject& arguments, const QString& toolName)
{
    if (!allowToolInvocation())
    {
        return makeToolResult(QStringLiteral("工具调用过于频繁，请稍后重试"), QJsonValue(), true);
    }

    if (toolName == QStringLiteral("serial_list_ports"))
    {
        _serialController->refreshPorts();
        const QStringList names = _serialController->portNames();
        const QStringList descriptions = _serialController->portDescriptions();
        QJsonArray ports;
        for (int index = 0; index < names.size(); ++index)
        {
            ports.append(QJsonObject{
                {QStringLiteral("name"), names.at(index)},
                {QStringLiteral("description"), descriptions.value(index)}});
        }
        const QJsonObject content{{QStringLiteral("ports"), ports}};
        return makeToolResult(QJsonDocument(content).toJson(QJsonDocument::Compact), content, false);
    }

    if (toolName == QStringLiteral("serial_get_status"))
    {
        const QStringList names = _serialController->portNames();
        const QStringList descriptions = _serialController->portDescriptions();
        QJsonArray ports;
        for (int index = 0; index < names.size(); ++index)
        {
            ports.append(QJsonObject{
                {QStringLiteral("name"), names.at(index)},
                {QStringLiteral("description"), descriptions.value(index)}});
        }
        const QJsonObject content{
            {QStringLiteral("connected"), _serialController->isOpen()},
            {QStringLiteral("port"), _serialController->portName()},
            {QStringLiteral("baud_rate"), _serialController->baudRate()},
            {QStringLiteral("data_bits"), _serialController->dataBits()},
            {QStringLiteral("parity"), _serialController->parity()},
            {QStringLiteral("stop_bits"), _serialController->stopBits()},
            {QStringLiteral("available_ports"), ports}};
        return makeToolResult(QJsonDocument(content).toJson(QJsonDocument::Compact), content, false);
    }

    if (toolName == QStringLiteral("serial_read"))
    {
        const qint64 afterId = qMax<qint64>(0, arguments.value(QStringLiteral("after_id")).toInteger(0));
        const int requestedLimit = arguments.value(QStringLiteral("limit")).toInt(100);
        const int limit = qBound(1, requestedLimit, kMaximumReadRecords);
        QJsonArray records;
        qsizetype responseBytes = 0;
        bool truncated = !_trafficRecords.isEmpty() && afterId > 0
            && afterId < static_cast<qint64>(_trafficRecords.first().id) - 1;
        quint64 nextCursor = static_cast<quint64>(afterId);
        for (const TrafficRecord& record : _trafficRecords)
        {
            if (record.id <= static_cast<quint64>(afterId))
            {
                continue;
            }
            if (records.size() >= limit || responseBytes + record.data.size() > kMaximumToolResponseBytes)
            {
                truncated = true;
                break;
            }
            const QJsonObject item{
                {QStringLiteral("id"), static_cast<qint64>(record.id)},
                {QStringLiteral("direction"), record.received ? QStringLiteral("rx") : QStringLiteral("tx")},
                {QStringLiteral("timestamp"), record.timestamp.toString(Qt::ISODateWithMs)},
                {QStringLiteral("length"), record.data.size()},
                {QStringLiteral("hex"), QString::fromLatin1(record.data.toHex(' ').toUpper())},
                {QStringLiteral("text"), QString::fromUtf8(record.data)}};
            records.append(item);
            responseBytes += record.data.size();
            nextCursor = record.id;
        }
        const QJsonObject content{
            {QStringLiteral("records"), records},
            {QStringLiteral("next_cursor"), static_cast<qint64>(nextCursor)},
            {QStringLiteral("truncated"), truncated}};
        return makeToolResult(QJsonDocument(content).toJson(QJsonDocument::Compact), content, false);
    }

    if (toolName == QStringLiteral("serial_open"))
    {
        const QString port = arguments.value(QStringLiteral("port")).toString();
        const int baudRate = arguments.value(QStringLiteral("baud_rate")).toInt(115200);
        const int dataBits = arguments.value(QStringLiteral("data_bits")).toInt(8);
        const QString parityText = arguments.value(QStringLiteral("parity")).toString(QStringLiteral("none"));
        const QString stopBitsText = arguments.value(QStringLiteral("stop_bits")).toString(QStringLiteral("1"));
        if (port.isEmpty() || baudRate < 1 || baudRate > 4000000 || dataBits < 5 || dataBits > 8)
        {
            return makeToolResult(QStringLiteral("串口参数无效"), QJsonValue(), true);
        }

        int parity = QSerialPort::NoParity;
        if (parityText == QStringLiteral("odd"))
        {
            parity = QSerialPort::OddParity;
        }
        else if (parityText == QStringLiteral("even"))
        {
            parity = QSerialPort::EvenParity;
        }
        else if (parityText == QStringLiteral("mark"))
        {
            parity = QSerialPort::MarkParity;
        }
        else if (parityText == QStringLiteral("space"))
        {
            parity = QSerialPort::SpaceParity;
        }
        else if (parityText != QStringLiteral("none"))
        {
            return makeToolResult(QStringLiteral("校验位必须是 none、odd、even、mark 或 space"), QJsonValue(), true);
        }

        int stopBits = QSerialPort::OneStop;
        if (stopBitsText == QStringLiteral("1.5"))
        {
            stopBits = QSerialPort::OneAndHalfStop;
        }
        else if (stopBitsText == QStringLiteral("2"))
        {
            stopBits = QSerialPort::TwoStop;
        }
        else if (stopBitsText != QStringLiteral("1"))
        {
            return makeToolResult(QStringLiteral("停止位必须是 1、1.5 或 2"), QJsonValue(), true);
        }

        if (!_serialController->openPort(port, baudRate, dataBits, parity, stopBits))
        {
            return makeToolResult(QStringLiteral("打开串口失败"), QJsonValue(), true);
        }
        return makeToolResult(QStringLiteral("串口已打开：%1 @ %2").arg(port).arg(baudRate),
                              QJsonObject{{QStringLiteral("connected"), true},
                                          {QStringLiteral("port"), port},
                                          {QStringLiteral("baud_rate"), baudRate}}, false);
    }

    if (toolName == QStringLiteral("serial_close"))
    {
        _serialController->closePort();
        return makeToolResult(QStringLiteral("串口已关闭"),
                              QJsonObject{{QStringLiteral("connected"), false}}, false);
    }

    if (toolName == QStringLiteral("serial_send"))
    {
        QString errorMessage;
        const QByteArray data = parseSendData(arguments, errorMessage);
        if (data.isEmpty())
        {
            return makeToolResult(errorMessage, QJsonValue(), true);
        }
        if (data.size() > kMaximumSendBytes)
        {
            return makeToolResult(QStringLiteral("单次发送不能超过 65536 字节"), QJsonValue(), true);
        }
        if (!_serialController->writeData(data))
        {
            return makeToolResult(QStringLiteral("串口发送失败"), QJsonValue(), true);
        }
        const QJsonObject content{
            {QStringLiteral("sent"), true},
            {QStringLiteral("length"), data.size()},
            {QStringLiteral("hex"), QString::fromLatin1(data.toHex(' ').toUpper())}};
        return makeToolResult(QJsonDocument(content).toJson(QJsonDocument::Compact), content, false);
    }

    return makeToolResult(QStringLiteral("未知串口工具"), QJsonValue(), true);
}

/*****************************************************
函数名称：QJsonArray SerialMcpServer::toolDefinitions() const
入口参数：无
出口参数：MCP工具定义数组
函数功能：描述串口读取和控制工具及其参数约束
*****************************************************/
QJsonArray SerialMcpServer::toolDefinitions() const
{
    const QJsonObject emptySchema{
        {QStringLiteral("type"), QStringLiteral("object")},
        {QStringLiteral("additionalProperties"), false}};

    const QJsonObject readProperties{
        {QStringLiteral("after_id"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("integer")},
            {QStringLiteral("minimum"), 0}}},
        {QStringLiteral("limit"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("integer")},
            {QStringLiteral("minimum"), 1},
            {QStringLiteral("maximum"), kMaximumReadRecords}}}};
    const QJsonObject readSchema{
        {QStringLiteral("type"), QStringLiteral("object")},
        {QStringLiteral("properties"), readProperties},
        {QStringLiteral("additionalProperties"), false}};

    const QJsonObject openProperties{
        {QStringLiteral("port"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("string")}}},
        {QStringLiteral("baud_rate"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("integer")},
            {QStringLiteral("minimum"), 1},
            {QStringLiteral("maximum"), 4000000},
            {QStringLiteral("default"), 115200}}},
        {QStringLiteral("data_bits"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("integer")},
            {QStringLiteral("enum"), QJsonArray{5, 6, 7, 8}},
            {QStringLiteral("default"), 8}}},
        {QStringLiteral("parity"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("string")},
            {QStringLiteral("enum"), QJsonArray{
                QStringLiteral("none"), QStringLiteral("odd"), QStringLiteral("even"),
                QStringLiteral("mark"), QStringLiteral("space")}},
            {QStringLiteral("default"), QStringLiteral("none")}}},
        {QStringLiteral("stop_bits"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("string")},
            {QStringLiteral("enum"), QJsonArray{
                QStringLiteral("1"), QStringLiteral("1.5"), QStringLiteral("2")}},
            {QStringLiteral("default"), QStringLiteral("1")}}}};
    const QJsonObject openSchema{
        {QStringLiteral("type"), QStringLiteral("object")},
        {QStringLiteral("properties"), openProperties},
        {QStringLiteral("required"), QJsonArray{QStringLiteral("port")}},
        {QStringLiteral("additionalProperties"), false}};

    const QJsonObject sendProperties{
        {QStringLiteral("data"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("string")}}},
        {QStringLiteral("encoding"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("string")},
            {QStringLiteral("enum"), QJsonArray{
                QStringLiteral("hex"), QStringLiteral("utf8")}},
            {QStringLiteral("default"), QStringLiteral("hex")}}}};
    const QJsonObject sendSchema{
        {QStringLiteral("type"), QStringLiteral("object")},
        {QStringLiteral("properties"), sendProperties},
        {QStringLiteral("required"), QJsonArray{QStringLiteral("data")}},
        {QStringLiteral("additionalProperties"), false}};

    const QJsonObject listPorts{
        {QStringLiteral("name"), QStringLiteral("serial_list_ports")},
        {QStringLiteral("title"), QStringLiteral("列出串口")},
        {QStringLiteral("description"), QStringLiteral("列出当前可用串口及设备描述。")},
        {QStringLiteral("inputSchema"), emptySchema},
        {QStringLiteral("annotations"), toolAnnotation(true, false, true)}};
    const QJsonObject getStatus{
        {QStringLiteral("name"), QStringLiteral("serial_get_status")},
        {QStringLiteral("title"), QStringLiteral("读取串口状态")},
        {QStringLiteral("description"), QStringLiteral("读取当前共享串口连接状态和参数。")},
        {QStringLiteral("inputSchema"), emptySchema},
        {QStringLiteral("annotations"), toolAnnotation(true, false, true)}};
    const QJsonObject readRecords{
        {QStringLiteral("name"), QStringLiteral("serial_read")},
        {QStringLiteral("title"), QStringLiteral("读取收发记录")},
        {QStringLiteral("description"), QStringLiteral("按游标读取 MCP 服务启用期间的串口收发原始记录。")},
        {QStringLiteral("inputSchema"), readSchema},
        {QStringLiteral("annotations"), toolAnnotation(true, false, true)}};
    const QJsonObject openPort{
        {QStringLiteral("name"), QStringLiteral("serial_open")},
        {QStringLiteral("title"), QStringLiteral("打开串口")},
        {QStringLiteral("description"), QStringLiteral("打开指定串口；如果已有连接，会先关闭旧连接。")},
        {QStringLiteral("inputSchema"), openSchema},
        {QStringLiteral("annotations"), toolAnnotation(false, false, false)}};
    const QJsonObject closePort{
        {QStringLiteral("name"), QStringLiteral("serial_close")},
        {QStringLiteral("title"), QStringLiteral("关闭串口")},
        {QStringLiteral("description"), QStringLiteral("关闭当前共享串口连接。")},
        {QStringLiteral("inputSchema"), emptySchema},
        {QStringLiteral("annotations"), toolAnnotation(false, false, true)}};
    const QJsonObject sendData{
        {QStringLiteral("name"), QStringLiteral("serial_send")},
        {QStringLiteral("title"), QStringLiteral("发送串口数据")},
        {QStringLiteral("description"), QStringLiteral("向当前串口发送数据。默认按 HEX 解析，单次最多 65536 字节。")},
        {QStringLiteral("inputSchema"), sendSchema},
        {QStringLiteral("annotations"), toolAnnotation(false, true, false)}};
    return QJsonArray{listPorts, getStatus, readRecords, openPort, closePort, sendData};
}

/*****************************************************
函数名称：QJsonObject SerialMcpServer::makeRpcError(const QJsonValue& id, int code, const QString& message) const
入口参数：id为JSON-RPC请求ID，code为错误码，message为错误信息
出口参数：JSON-RPC错误响应
函数功能：构造统一的JSON-RPC错误对象
*****************************************************/
QJsonObject SerialMcpServer::makeRpcError(const QJsonValue& id, int code, const QString& message) const
{
    return QJsonObject{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                       {QStringLiteral("id"), id},
                       {QStringLiteral("error"), QJsonObject{
                           {QStringLiteral("code"), code},
                           {QStringLiteral("message"), message}}}};
}

/*****************************************************
函数名称：QByteArray SerialMcpServer::makeHttpResponse(int status, const QByteArray& contentType, const QByteArray& body) const
入口参数：status为HTTP状态码，contentType为内容类型，body为响应体
出口参数：完整HTTP响应
函数功能：生成关闭连接式HTTP/1.1响应
*****************************************************/
QByteArray SerialMcpServer::makeHttpResponse(int status, const QByteArray& contentType, const QByteArray& body) const
{
    QByteArray response = QByteArrayLiteral("HTTP/1.1 ") + QByteArray::number(status) + ' '
        + httpReasonPhrase(status) + QByteArrayLiteral("\r\nConnection: close\r\n");
    if (!contentType.isEmpty())
    {
        response += QByteArrayLiteral("Content-Type: ") + contentType + QByteArrayLiteral("\r\n");
    }
    response += QByteArrayLiteral("Content-Length: ") + QByteArray::number(body.size())
        + QByteArrayLiteral("\r\nCache-Control: no-store\r\n\r\n") + body;
    return response;
}

/*****************************************************
函数名称：bool SerialMcpServer::isOriginAllowed(const QByteArray& origin) const
入口参数：origin为HTTP Origin请求头
出口参数：是否允许请求
函数功能：仅允许本机来源，阻止浏览器跨站访问本地串口服务
*****************************************************/
bool SerialMcpServer::isOriginAllowed(const QByteArray& origin) const
{
    if (origin.isEmpty())
    {
        return true;
    }
    const QUrl url(QString::fromUtf8(origin));
    const QString host = url.host().toLower();
    return url.isValid()
        && (url.scheme() == QStringLiteral("http") || url.scheme() == QStringLiteral("https"))
        && (host == QStringLiteral("localhost") || host == QStringLiteral("127.0.0.1")
            || host == QStringLiteral("[::1]") || host == QStringLiteral("::1"));
}

/*****************************************************
函数名称：bool SerialMcpServer::allowToolInvocation()
入口参数：无
出口参数：本次工具调用是否允许执行
函数功能：限制本地 MCP 工具调用频率，避免失控循环占用串口
*****************************************************/
bool SerialMcpServer::allowToolInvocation()
{
    if (!_toolRateTimer.isValid())
    {
        _toolRateTimer.start();
    }
    const qint64 now = _toolRateTimer.elapsed();
    while (!_toolInvocationTimes.isEmpty()
        && _toolInvocationTimes.head() < now - kToolRateWindowMilliseconds)
    {
        _toolInvocationTimes.dequeue();
    }
    if (_toolInvocationTimes.size() >= kMaximumToolCallsPerWindow)
    {
        return false;
    }
    _toolInvocationTimes.enqueue(now);
    return true;
}

/*****************************************************
函数名称：void SerialMcpServer::appendTrafficRecord(bool received, const QByteArray& data)
入口参数：received为接收方向标志，data为原始串口数据
出口参数：无
函数功能：按数量和总字节数上限保存通信记录
*****************************************************/
void SerialMcpServer::appendTrafficRecord(bool received, const QByteArray& data)
{
    if (data.isEmpty() || data.size() > kMaximumStoredTrafficRecordBytes)
    {
        return;
    }
    _trafficRecords.append(TrafficRecord{_nextTrafficId++, received, QDateTime::currentDateTime(), data});
    _trafficBytes += data.size();
    while (_trafficRecords.size() > kMaximumTrafficRecords || _trafficBytes > kMaximumTrafficBytes)
    {
        _trafficBytes -= _trafficRecords.first().data.size();
        _trafficRecords.removeFirst();
    }
}

/*****************************************************
函数名称：QJsonObject SerialMcpServer::makeToolResult(const QString& text, const QJsonValue& structuredContent, bool isError) const
入口参数：text为文本结果，structuredContent为结构化结果，isError为工具执行错误标志
出口参数：MCP工具结果对象
函数功能：构造兼容新旧客户端的工具响应
*****************************************************/
QJsonObject SerialMcpServer::makeToolResult(
    const QString& text, const QJsonValue& structuredContent, bool isError) const
{
    QJsonObject result{{QStringLiteral("resultType"), QStringLiteral("complete")},
                       {QStringLiteral("content"), QJsonArray{textContent(text)}},
                       {QStringLiteral("isError"), isError}};
    if (!structuredContent.isUndefined())
    {
        result.insert(QStringLiteral("structuredContent"), structuredContent);
    }
    return result;
}

/*****************************************************
函数名称：QByteArray SerialMcpServer::parseSendData(const QJsonObject& arguments, QString& errorMessage) const
入口参数：arguments为发送工具参数，errorMessage为错误信息输出
出口参数：待发送原始字节
函数功能：根据 encoding 参数解析 HEX 或 UTF-8 发送内容
*****************************************************/
QByteArray SerialMcpServer::parseSendData(const QJsonObject& arguments, QString& errorMessage) const
{
    const QString input = arguments.value(QStringLiteral("data")).toString();
    if (input.isEmpty())
    {
        errorMessage = QStringLiteral("data 不能为空");
        return QByteArray();
    }

    const QString encoding = arguments.value(QStringLiteral("encoding")).toString(QStringLiteral("hex"));
    if (encoding == QStringLiteral("utf8"))
    {
        return input.toUtf8();
    }
    if (encoding != QStringLiteral("hex"))
    {
        errorMessage = QStringLiteral("encoding 必须是 hex 或 utf8");
        return QByteArray();
    }

    QByteArray normalized;
    normalized.reserve(input.size());
    for (const QChar character : input)
    {
        if (character.isSpace())
        {
            continue;
        }
        const char value = character.toLatin1();
        const bool isHex = (value >= '0' && value <= '9')
            || (value >= 'a' && value <= 'f') || (value >= 'A' && value <= 'F');
        if (!isHex)
        {
            errorMessage = QStringLiteral("HEX 数据只能包含 0-9、A-F 和空白字符");
            return QByteArray();
        }
        normalized.append(value);
    }
    if (normalized.isEmpty() || normalized.size() % 2 != 0)
    {
        errorMessage = QStringLiteral("HEX 数据必须包含完整的字节");
        return QByteArray();
    }
    return QByteArray::fromHex(normalized);
}
