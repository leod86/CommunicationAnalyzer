#ifndef SERIALMCPSERVER_H
#define SERIALMCPSERVER_H

#include <QByteArray>
#include <QDateTime>
#include <QElapsedTimer>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QObject>
#include <QQueue>

class QTcpServer;
class QTcpSocket;
class SerialController;

class SerialMcpServer final : public QObject
{
    Q_OBJECT

public:
    // 创建串口 MCP 服务并绑定现有串口控制器。
    explicit SerialMcpServer(SerialController* serialController, QObject* parent = nullptr);
    // 释放 MCP 服务并停止监听。
    ~SerialMcpServer() override;

    // 启动或停止本地 MCP 服务。
    bool setEnabled(bool enabled);
    // 获取 MCP 服务是否已启用。
    bool isEnabled() const;
    // 获取本地 MCP 接入地址。
    QString endpoint() const;

signals:
    // 通知设置页更新 MCP 服务运行状态和提示信息。
    void stateChanged(bool running, const QString& message);

private slots:
    // 接收新的本地 HTTP 连接。
    void handleNewConnection();
    // 接收串口控制器发布的原始接收数据。
    void handleReceivedData(const QByteArray& data);
    // 接收串口控制器发布的原始发送数据。
    void handleSentData(const QByteArray& data);

private:
    // MCP 传输请求解析状态。
    enum class RequestParseResult
    {
        Incomplete,
        Complete,
        Invalid
    };

    // 一条供 MCP 分页读取的串口通信记录。
    struct TrafficRecord
    {
        quint64 id{0};
        bool received{false};
        QDateTime timestamp;
        QByteArray data;
    };

    // 一个待处理的 HTTP 请求。
    struct HttpRequest
    {
        QByteArray method;
        QByteArray target;
        QHash<QByteArray, QByteArray> headers;
        QByteArray body;
    };

    // 处理指定套接字中的完整 HTTP 请求。
    void processSocket(QTcpSocket* socket);
    // 解析 HTTP 请求头和定长请求体。
    RequestParseResult parseHttpRequest(QByteArray& buffer, HttpRequest& request) const;
    // 分发一个 MCP JSON-RPC 请求并生成 HTTP 响应。
    void dispatchRequest(QTcpSocket* socket, const HttpRequest& request);
    // 处理 MCP JSON-RPC 方法并返回响应对象。
    QJsonObject dispatchRpc(const QJsonObject& rpc, const QByteArray& protocolVersion, int& httpStatus);
    // 处理串口工具调用。
    QJsonObject handleToolCall(const QJsonObject& arguments, const QString& toolName);
    // 生成 MCP 工具列表。
    QJsonArray toolDefinitions() const;
    // 生成 JSON-RPC 错误响应。
    QJsonObject makeRpcError(const QJsonValue& id, int code, const QString& message) const;
    // 生成 HTTP 响应并关闭当前连接。
    QByteArray makeHttpResponse(int status, const QByteArray& contentType, const QByteArray& body) const;
    // 验证本地请求来源，阻止非本地 Origin。
    bool isOriginAllowed(const QByteArray& origin) const;
    // 判断当前工具调用是否超过速率限制。
    bool allowToolInvocation();
    // 追加一条通信记录并限制内存占用。
    void appendTrafficRecord(bool received, const QByteArray& data);
    // 构造成功或失败的工具结果。
    QJsonObject makeToolResult(const QString& text, const QJsonValue& structuredContent, bool isError) const;
    // 解析工具参数中的 HEX 或 UTF-8 数据。
    QByteArray parseSendData(const QJsonObject& arguments, QString& errorMessage) const;

    SerialController* _serialController{nullptr};
    QTcpServer* _server{nullptr};
    QHash<QTcpSocket*, QByteArray> _socketBuffers;
    QList<TrafficRecord> _trafficRecords;
    QQueue<qint64> _toolInvocationTimes;
    QElapsedTimer _toolRateTimer;
    quint64 _nextTrafficId{1};
    qsizetype _trafficBytes{0};
    bool _enabled{false};
};

#endif // SERIALMCPSERVER_H
