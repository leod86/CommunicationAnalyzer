#include "update/UpdateManager.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProgressDialog>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QWidget>
#include <QDebug>

namespace
{
constexpr auto kInstallerAssetName = "CommunicationAnalyzer-x64.msi";
constexpr auto kChecksumAssetName = "CommunicationAnalyzer-x64.msi.sha256";
constexpr auto kRateLimitResetHeader = "X-RateLimit-Reset";
constexpr auto kMirrorManifestName = "/latest.json";
constexpr int kManifestTransferTimeoutMs = 15'000;
constexpr int kInstallerTransferTimeoutMs = 30'000;
constexpr int kMaxDownloadRetries = 5;
// 断线重试的退避时间（秒），按重试次数依次取用。
constexpr int kRetryDelaysSeconds[kMaxDownloadRetries] = {2, 4, 8, 16, 30};
constexpr int kMaxFailureBodyCharacters = 200;
constexpr auto kSkippedVersionKey = "update/skippedVersion";
// 与监视页共用的配置存储名称。
constexpr auto kSettingsName = "CommunicationAnalyzer";

/*****************************************************
函数名称：bool parseVersionText(const QString& text, QVersionNumber* version)
入口参数：text为版本号文本（可带前缀v），version为解析结果输出
出口参数：解析成功返回true，否则返回false
函数功能：把 tag_name 或清单 version 字段统一解析为完整三段版本号
*****************************************************/
bool parseVersionText(const QString& text, QVersionNumber* version)
{
    QString versionText = text.trimmed();
    if (versionText.startsWith(QLatin1Char('v'), Qt::CaseInsensitive))
    {
        versionText.remove(0, 1);
    }
    qsizetype parsedLength = 0;
    const QVersionNumber parsed = QVersionNumber::fromString(versionText, &parsedLength);
    if (parsedLength != versionText.size() || parsed.isNull())
    {
        return false;
    }

    *version = parsed;
    return true;
}

/*****************************************************
函数名称：bool isRetryableDownloadStatus(int httpStatus)
入口参数：httpStatus为响应的 HTTP 状态码
出口参数：该状态码是否属于可重试的临时故障
函数功能：仅把 5xx、408 和 429 视为服务端临时故障
*****************************************************/
bool isRetryableDownloadStatus(int httpStatus)
{
    return httpStatus == 408 || httpStatus == 429
           || (httpStatus >= 500 && httpStatus <= 599);
}

/*****************************************************
函数名称：QString summarizeFailureBody(const QByteArray& body)
入口参数：body为失败响应的原始正文
出口参数：可供展示的失败摘要，正文为空时返回空字符串
函数功能：优先提取 GitHub 错误 JSON 的 message 字段，否则退化为纯文本并统一截断长度
*****************************************************/
QString summarizeFailureBody(const QByteArray& body)
{
    const QByteArray trimmedBody = body.trimmed();
    if (trimmedBody.isEmpty())
    {
        return {};
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(trimmedBody, &parseError);
    QString summary;
    if (parseError.error == QJsonParseError::NoError && document.isObject())
    {
        summary = document.object().value(QStringLiteral("message")).toString();
    }
    if (summary.isEmpty())
    {
        summary = QString::fromUtf8(trimmedBody);
    }

    summary = summary.simplified();
    if (summary.size() > kMaxFailureBodyCharacters)
    {
        summary.truncate(kMaxFailureBodyCharacters);
        summary += QStringLiteral("…");
    }
    return summary;
}

/*****************************************************
函数名称：QString appendFailureDetail(const QString& summary, const QString& detail)
入口参数：summary为失败结论，detail为服务端返回的细节
出口参数：拼接后的完整提示文本
函数功能：按细节是否存在选择句号或冒号结尾，避免出现空冒号
*****************************************************/
QString appendFailureDetail(const QString& summary, const QString& detail)
{
    if (detail.isEmpty())
    {
        return summary + QStringLiteral("。");
    }
    return summary + QStringLiteral("：") + detail;
}

/*****************************************************
函数名称：QString describeNetworkFailure(const QNetworkReply* reply, const QByteArray& body)
入口参数：reply为已结束的网络响应，body为该响应的正文
出口参数：可直接展示给用户的失败原因
函数功能：结合 HTTP 状态码、GitHub 错误正文和限流重置时间生成可读的失败原因
*****************************************************/
QString describeNetworkFailure(const QNetworkReply* reply, const QByteArray& body)
{
    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString detail = summarizeFailureBody(body);

    // 连接层失败没有 HTTP 状态码（如断开、超时），以中文结论为主，Qt 的描述作为补充。
    if (httpStatus == 0)
    {
        const QString errorString = reply->errorString();
        if (errorString.isEmpty() || errorString == QStringLiteral("Unknown error"))
        {
            return QStringLiteral("网络请求失败，请检查网络或代理设置。");
        }
        return QStringLiteral("网络请求失败（%1），请检查网络或代理设置。").arg(errorString);
    }

    // GitHub 对未登录请求按出口 IP 限制为每小时 60 次，重试时间可从响应头推算。
    if (httpStatus == 403 && detail.contains(QStringLiteral("rate limit"), Qt::CaseInsensitive))
    {
        QString message = QStringLiteral(
            "GitHub 访问频率受限：未登录请求每小时 60 次，且额度按出口 IP 计算。");
        bool resetParsed = false;
        const qint64 resetSeconds = reply->rawHeader(kRateLimitResetHeader).toLongLong(&resetParsed);
        if (resetParsed)
        {
            const QDateTime resetTime = QDateTime::fromSecsSinceEpoch(resetSeconds);
            const qint64 remainingSeconds = QDateTime::currentDateTimeUtc().secsTo(resetTime);
            if (remainingSeconds > 0)
            {
                message += QStringLiteral("\n请约 %1 分钟后重试。").arg((remainingSeconds + 59) / 60);
            }
        }
        return message;
    }

    if (httpStatus == 403)
    {
        return appendFailureDetail(QStringLiteral("GitHub 拒绝了更新请求（HTTP 403）"), detail);
    }
    if (httpStatus == 404)
    {
        return QStringLiteral("未找到 GitHub 发布信息（HTTP 404），请确认网络可以访问 github.com。");
    }
    if (httpStatus == 407)
    {
        return appendFailureDetail(QStringLiteral("代理服务器需要认证（HTTP 407）"), detail);
    }
    return appendFailureDetail(QStringLiteral("请求 GitHub 失败（HTTP %1）").arg(httpStatus), detail);
}
} // namespace

/*****************************************************
函数名称：UpdateManager::UpdateManager(QWidget* parentWindow, QObject* parent)
入口参数：parentWindow为更新提示的父窗口，parent为父对象
出口参数：无
函数功能：创建用于访问 GitHub Release 的更新管理器
*****************************************************/
UpdateManager::UpdateManager(QWidget* parentWindow, QObject* parent)
    : QObject(parent)
    , _parentWindow(parentWindow)
    , _networkManager(new QNetworkAccessManager(this))
{
}

/*****************************************************
函数名称：UpdateManager::~UpdateManager()
入口参数：无
出口参数：无
函数功能：清理下载过程遗留的未完成安装包
*****************************************************/
UpdateManager::~UpdateManager()
{
    if (!_installationStarted)
    {
        discardInstaller();
    }
}

/*****************************************************
函数名称：void UpdateManager::checkForUpdates()
入口参数：无
出口参数：无
函数功能：仅对 MSI 安装目录内的程序异步检查 GitHub 最新正式版
*****************************************************/
void UpdateManager::checkForUpdates()
{
    if (_checkStarted || !QFileInfo::exists(updateScriptPath()))
    {
        return;
    }

    if (showPreviousUpdateFailure())
    {
        return;
    }

    _checkStarted = true;
    requestMirrorManifest();
}

/*****************************************************
函数名称：QNetworkRequest UpdateManager::createRequest(const QUrl& url) const
入口参数：url 为 GitHub API 或发布资源地址
出口参数：配置完成的网络请求
函数功能：设置请求头和安全重定向策略，GitHub 专用 Accept 头只对 api.github.com 添加
*****************************************************/
QNetworkRequest UpdateManager::createRequest(const QUrl& url) const
{
    QNetworkRequest request(url);
    if (url.host().compare(QStringLiteral("api.github.com"), Qt::CaseInsensitive) == 0)
    {
        request.setRawHeader("Accept", "application/vnd.github+json");
    }
    request.setRawHeader("User-Agent", "CommunicationAnalyzer/" COMMUNICATION_ANALYZER_VERSION);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(60'000);
    return request;
}

/*****************************************************
函数名称：void UpdateManager::requestMirrorManifest()
入口参数：无
出口参数：无
函数功能：优先请求 R2 镜像上的 latest.json 更新清单
*****************************************************/
void UpdateManager::requestMirrorManifest()
{
    QNetworkRequest request = createRequest(mirrorManifestUrl());
    request.setTransferTimeout(kManifestTransferTimeoutMs);
    QNetworkReply* reply = _networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        handleMirrorManifestReply(reply);
    });
}

/*****************************************************
函数名称：QUrl UpdateManager::mirrorManifestUrl() const
入口参数：无
出口参数：镜像清单 latest.json 的完整地址
函数功能：拼接镜像基础地址与清单文件名
*****************************************************/
QUrl UpdateManager::mirrorManifestUrl() const
{
    return QUrl(QStringLiteral(COMMUNICATION_ANALYZER_UPDATE_MIRROR)
                + QString::fromLatin1(kMirrorManifestName));
}

/*****************************************************
函数名称：void UpdateManager::handleMirrorManifestReply(QNetworkReply* reply)
入口参数：reply 为镜像 latest.json 响应
出口参数：无
函数功能：解析镜像清单，无效时回退 GitHub；版本不新时直接结束
*****************************************************/
void UpdateManager::handleMirrorManifestReply(QNetworkReply* reply)
{
    const QByteArray responseBody = reply->readAll();
    const QNetworkReply::NetworkError networkError = reply->error();
    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    reply->deleteLater();

    // 镜像不可用或内容无效只记录日志，不打扰用户，直接回退到 GitHub。
    if (networkError != QNetworkReply::NoError)
    {
        qWarning().noquote() << "Update mirror request failed:" << networkError << "HTTP" << httpStatus;
        requestLatestRelease();
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(responseBody, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        qWarning() << "Update mirror manifest is not a valid JSON object";
        requestLatestRelease();
        return;
    }

    const QJsonObject manifest = document.object();
    const QString installerText = manifest.value(QStringLiteral("installer")).toString();
    const QString checksumText = manifest.value(QStringLiteral("sha256")).toString().trimmed();
    const QJsonValue sizeValue = manifest.value(QStringLiteral("size"));
    QVersionNumber manifestVersion;
    static const QRegularExpression checksumPattern(QStringLiteral("^[0-9a-fA-F]{64}$"));
    const bool sizeValid = sizeValue.isUndefined()
                           || (sizeValue.isDouble() && sizeValue.toDouble() > 0);
    if (!parseVersionText(manifest.value(QStringLiteral("version")).toString(), &manifestVersion)
        || installerText.isEmpty() || !sizeValid
        || !checksumPattern.match(checksumText).hasMatch())
    {
        qWarning() << "Update mirror manifest has invalid fields";
        requestLatestRelease();
        return;
    }

    // installer 为相对路径，解析结果必须是 https 地址才允许下载。
    const QUrl installerUrl = mirrorManifestUrl().resolved(QUrl(installerText));
    if (installerUrl.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) != 0
        || installerUrl.host().isEmpty())
    {
        qWarning() << "Update mirror installer URL is not a valid https address";
        requestLatestRelease();
        return;
    }

    // 清单有效但版本不新或已被跳过时直接结束，不再查询 GitHub。
    applyAvailableRelease(manifestVersion, installerUrl, QUrl(),
                          checksumText.toLatin1().toLower());
}

/*****************************************************
函数名称：void UpdateManager::requestLatestRelease()
入口参数：无
出口参数：无
函数功能：请求 GitHub 最新正式 Release 的版本和资源列表
*****************************************************/
void UpdateManager::requestLatestRelease()
{
    // 回退到 GitHub 时丢弃镜像清单带来的校验值，改由校验文件提供。
    _expectedChecksum.clear();
    const QUrl releaseUrl(
        QStringLiteral("https://api.github.com/repos/")
        + QStringLiteral(COMMUNICATION_ANALYZER_UPDATE_REPOSITORY)
        + QStringLiteral("/releases/latest"));
    QNetworkReply* reply = _networkManager->get(createRequest(releaseUrl));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        handleLatestReleaseReply(reply);
    });
}

/*****************************************************
函数名称：QUrl UpdateManager::findAssetUrl(const QJsonArray& assets, const QString& assetName) const
入口参数：assets 为 Release 资源列表，assetName 为目标资源文件名
出口参数：目标资源下载地址，未找到时返回空地址
函数功能：按固定文件名定位 MSI 或其 SHA-256 校验文件
*****************************************************/
QUrl UpdateManager::findAssetUrl(const QJsonArray& assets, const QString& assetName) const
{
    for (const QJsonValue& assetValue : assets)
    {
        const QJsonObject asset = assetValue.toObject();
        if (asset.value(QStringLiteral("name")).toString() == assetName)
        {
            return QUrl(asset.value(QStringLiteral("browser_download_url")).toString());
        }
    }

    return {};
}

/*****************************************************
函数名称：void UpdateManager::requestChecksum(const QUrl& checksumUrl)
入口参数：checksumUrl 为 MSI SHA-256 文件地址
出口参数：无
函数功能：下载并验证 SHA-256 文本格式
*****************************************************/
void UpdateManager::requestChecksum(const QUrl& checksumUrl)
{
    QNetworkReply* reply = _networkManager->get(createRequest(checksumUrl));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        handleChecksumReply(reply);
    });
}

/*****************************************************
函数名称：void UpdateManager::requestInstaller(const QUrl& installerUrl)
入口参数：installerUrl 为 MSI 安装包下载地址
出口参数：无
函数功能：创建临时 .part 文件并开始异步下载，支持断点续传
*****************************************************/
void UpdateManager::requestInstaller(const QUrl& installerUrl)
{
    const QString temporaryDirectory = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    if (temporaryDirectory.isEmpty())
    {
        return;
    }

    _installerUrl = installerUrl;
    _downloadedInstallerPath = QDir(temporaryDirectory).filePath(
        QStringLiteral("CommunicationAnalyzer-%1-x64.msi").arg(_availableVersion.toString()));
    _installerPartPath = _downloadedInstallerPath + QStringLiteral(".part");
    QFile::remove(_downloadedInstallerPath);
    // 新的下载任务总是从空的 .part 开始，续传只在同一次任务的重试之间发生。
    QFile::remove(_installerPartPath);

    _downloadCancelled = false;
    _installerWriteFailed = false;
    _installerWriteModeResolved = false;
    _downloadRetryCount = 0;
    _resumeOffset = 0;
    ++_retryToken;

    showDownloadProgress();
    startInstallerDownload();
}

/*****************************************************
函数名称：void UpdateManager::startInstallerDownload()
入口参数：无
出口参数：无
函数功能：按 .part 已有字节数发起首次下载或带 Range 头的续传请求
*****************************************************/
void UpdateManager::startInstallerDownload()
{
    _installerWriteModeResolved = false;
    const qint64 existingBytes = QFileInfo(_installerPartPath).size();
    _resumeOffset = existingBytes > 0 ? existingBytes : 0;

    _installerFile.setFileName(_installerPartPath);
    // 续传时以 Append 方式打开，保证已下载字节不被覆盖。
    const QIODevice::OpenMode openMode = _resumeOffset > 0
                                             ? (QIODevice::WriteOnly | QIODevice::Append)
                                             : (QIODevice::WriteOnly | QIODevice::Truncate);
    if (!_installerFile.open(openMode))
    {
        discardInstaller();
        closeDownloadProgress();
        showUpdateError(QStringLiteral("无法创建更新安装包的临时文件。"));
        return;
    }

    QNetworkRequest request = createRequest(_installerUrl);
    request.setTransferTimeout(kInstallerTransferTimeoutMs);
    if (_resumeOffset > 0)
    {
        request.setRawHeader("Range", QByteArrayLiteral("bytes=")
                                            + QByteArray::number(_resumeOffset) + '-');
    }

    QNetworkReply* reply = _networkManager->get(request);
    _installerReply = reply;
    connect(reply, &QNetworkReply::readyRead, this, [this, reply]() {
        handleInstallerData(reply);
    });
    connect(reply, &QNetworkReply::downloadProgress, this,
            [this](qint64 receivedBytes, qint64 totalBytes) {
        updateDownloadProgress(receivedBytes, totalBytes);
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        handleInstallerFinished(reply);
    });
}

/*****************************************************
函数名称：bool UpdateManager::resolveInstallerResponseMode(int httpStatus)
入口参数：httpStatus 为安装包下载响应的 HTTP 状态码
出口参数：写入模式是否确定成功
函数功能：206 继续追加，200 丢弃已有字节后从零写入
*****************************************************/
bool UpdateManager::resolveInstallerResponseMode(int httpStatus)
{
    if (httpStatus == 206)
    {
        // 服务器接受了 Range 请求，文件已以 Append 方式打开在续传起点。
        _installerWriteModeResolved = true;
        return true;
    }

    if (httpStatus == 200)
    {
        // 服务器忽略了 Range 请求，重新打开文件截断已有字节后从头写入。
        _installerFile.close();
        _installerFile.setFileName(_installerPartPath);
        if (!_installerFile.open(QIODevice::WriteOnly | QIODevice::Truncate))
        {
            _installerWriteFailed = true;
            return false;
        }
        _resumeOffset = 0;
        _installerWriteModeResolved = true;
        return true;
    }

    return false;
}

/*****************************************************
函数名称：bool UpdateManager::scheduleInstallerRetry()
入口参数：无
出口参数：返回是否已安排重试，次数用尽时返回 false
函数功能：按 2/4/8/16/30 秒退避安排下一次下载，并在进度框提示重试信息
*****************************************************/
bool UpdateManager::scheduleInstallerRetry()
{
    if (_downloadRetryCount >= kMaxDownloadRetries)
    {
        return false;
    }

    const int delaySeconds = kRetryDelaysSeconds[_downloadRetryCount];
    ++_downloadRetryCount;
    const int token = ++_retryToken;

    if (_downloadProgressDialog != nullptr)
    {
        _downloadProgressDialog->setRange(0, 0);
        _downloadProgressDialog->setLabelText(
            QStringLiteral("网络中断，%1 秒后第 %2 次重试…")
                .arg(delaySeconds)
                .arg(_downloadRetryCount));
    }

    // 取消下载或重新开始时会递增 _retryToken，使待执行的重试自动失效。
    QTimer::singleShot(delaySeconds * 1000, this, [this, token]() {
        if (_downloadCancelled || token != _retryToken)
        {
            return;
        }
        startInstallerDownload();
    });
    return true;
}

/*****************************************************
函数名称：void UpdateManager::resetDownloadRetry()
入口参数：无
出口参数：无
函数功能：复位断点续传与重试状态，并作废待执行的重试定时器
*****************************************************/
void UpdateManager::resetDownloadRetry()
{
    ++_retryToken;
    _downloadRetryCount = 0;
    _resumeOffset = 0;
    _installerWriteModeResolved = false;
    _installerWriteFailed = false;
}

/*****************************************************
函数名称：void UpdateManager::handleLatestReleaseReply(QNetworkReply* reply)
入口参数：reply 为 GitHub 最新 Release 响应
出口参数：无
函数功能：比较线上与当前版本，并获取更新所需资源地址
*****************************************************/
void UpdateManager::handleLatestReleaseReply(QNetworkReply* reply)
{
    const QByteArray responseBody = reply->readAll();
    const QNetworkReply::NetworkError networkError = reply->error();
    if (networkError != QNetworkReply::NoError)
    {
        const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString failureReason = describeNetworkFailure(reply, responseBody);
        reply->deleteLater();
        // 启动时自动检查失败只记录日志，避免离线或内网环境每次启动都弹窗。
        qWarning().noquote() << "Update check failed:" << networkError << "HTTP" << httpStatus
                             << failureReason;
        return;
    }
    reply->deleteLater();
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(responseBody, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        qWarning() << "Update release metadata is invalid";
        return;
    }

    const QJsonObject release = document.object();
    if (release.value(QStringLiteral("draft")).toBool()
        || release.value(QStringLiteral("prerelease")).toBool())
    {
        return;
    }

    QVersionNumber releaseVersion;
    if (!parseVersionText(release.value(QStringLiteral("tag_name")).toString(), &releaseVersion))
    {
        return;
    }

    const QJsonArray assets = release.value(QStringLiteral("assets")).toArray();
    const QUrl installerUrl = findAssetUrl(assets, QString::fromLatin1(kInstallerAssetName));
    const QUrl checksumUrl = findAssetUrl(assets, QString::fromLatin1(kChecksumAssetName));
    if (!installerUrl.isValid() || !checksumUrl.isValid())
    {
        qWarning() << "Update release does not contain the required MSI assets";
        return;
    }

    // GitHub 路径不随清单下发校验值，稍后单独下载 SHA-256 文件。
    applyAvailableRelease(releaseVersion, installerUrl, checksumUrl, QByteArray());
}

/*****************************************************
函数名称：bool UpdateManager::applyAvailableRelease(const QVersionNumber& version,
                                                     const QUrl& installerUrl,
                                                     const QUrl& checksumUrl,
                                                     const QByteArray& expectedChecksum)
入口参数：version 为线上版本，installerUrl 为安装包地址，checksumUrl 为校验文件地址，
          expectedChecksum 为清单直接下发的校验值（可为空）
出口参数：返回是否已应用并弹出更新确认框
函数功能：镜像清单与 GitHub Release 共用版本校验、跳过版本判断和新版本提示
*****************************************************/
bool UpdateManager::applyAvailableRelease(const QVersionNumber& version, const QUrl& installerUrl,
                                          const QUrl& checksumUrl,
                                          const QByteArray& expectedChecksum)
{
    const QVersionNumber currentVersion = QVersionNumber::fromString(
        QStringLiteral(COMMUNICATION_ANALYZER_VERSION));
    if (version.isNull() || currentVersion.isNull()
        || QVersionNumber::compare(version, currentVersion) <= 0)
    {
        return false;
    }

    // 用户已选择跳过该版本时不再提示，更新的版本发布后照常提示。
    if (QSettings(QString::fromLatin1(kSettingsName), QString::fromLatin1(kSettingsName))
            .value(QString::fromLatin1(kSkippedVersionKey)).toString() == version.toString())
    {
        return false;
    }

    _availableVersion = version;
    _installerUrl = installerUrl;
    _checksumUrl = checksumUrl;
    _expectedChecksum = expectedChecksum;
    showUpdateConfirmation();
    return true;
}

/*****************************************************
函数名称：void UpdateManager::handleChecksumReply(QNetworkReply* reply)
入口参数：reply 为 SHA-256 文件响应
出口参数：无
函数功能：提取可信 SHA-256 值并开始下载 MSI
*****************************************************/
void UpdateManager::handleChecksumReply(QNetworkReply* reply)
{
    const QByteArray responseBody = reply->readAll().trimmed();
    const QNetworkReply::NetworkError networkError = reply->error();
    if (networkError != QNetworkReply::NoError)
    {
        const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString failureReason = describeNetworkFailure(reply, responseBody);
        reply->deleteLater();
        qWarning().noquote() << "Update checksum download failed:" << networkError << "HTTP" << httpStatus;
        showUpdateError(QStringLiteral("无法下载更新校验文件。\n%1").arg(failureReason));
        return;
    }
    reply->deleteLater();
    static const QRegularExpression checksumPattern(
        QStringLiteral("^\\s*([0-9a-fA-F]{64})(?:\\s|$)"));
    const QRegularExpressionMatch checksumMatch = checksumPattern.match(QString::fromLatin1(responseBody));
    if (!checksumMatch.hasMatch())
    {
        qWarning() << "Update checksum has an invalid format";
        showUpdateError(QStringLiteral("更新校验文件格式无效。"));
        return;
    }

    _expectedChecksum = checksumMatch.captured(1).toLatin1().toLower();
    requestInstaller(_installerUrl);
}

/*****************************************************
函数名称：void UpdateManager::handleInstallerData(QNetworkReply* reply)
入口参数：reply 为 MSI 下载响应
出口参数：无
函数功能：将收到的 MSI 数据写入临时 .part 文件，首次收到数据时确定写入模式
*****************************************************/
void UpdateManager::handleInstallerData(QNetworkReply* reply)
{
    if (!_installerWriteModeResolved)
    {
        const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        // 非 200/206 响应不写入文件，交由结束处理决定重试或报错。
        if (httpStatus != 200 && httpStatus != 206)
        {
            reply->readAll();
            return;
        }
        if (!resolveInstallerResponseMode(httpStatus))
        {
            reply->readAll();
            return;
        }
    }

    const QByteArray data = reply->readAll();
    if (_installerFile.write(data) != data.size())
    {
        _installerWriteFailed = true;
        reply->abort();
    }
}

/*****************************************************
函数名称：void UpdateManager::handleInstallerFinished(QNetworkReply* reply)
入口参数：reply 为 MSI 下载响应
出口参数：无
函数功能：处理断线重试与断点续传，校验通过后重命名为最终安装包
*****************************************************/
void UpdateManager::handleInstallerFinished(QNetworkReply* reply)
{
    const QNetworkReply::NetworkError networkError = reply->error();
    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QByteArray responseBody;
    if (networkError == QNetworkReply::NoError)
    {
        // 正常结束前写入剩余数据，避免漏掉最后一段安装包内容。
        handleInstallerData(reply);
    }
    else
    {
        // 失败时保留响应正文用于诊断，并避免把错误页面当作安装包写入临时文件。
        responseBody = reply->readAll();
    }
    const QString failureReason = (networkError == QNetworkReply::NoError)
                                      ? QString()
                                      : describeNetworkFailure(reply, responseBody);
    reply->deleteLater();
    _installerReply = nullptr;
    _installerFile.close();

    if (_installerWriteFailed)
    {
        qWarning() << "Failed to write the update installer temporary file";
        discardInstaller();
        closeDownloadProgress();
        showUpdateError(QStringLiteral("写入更新安装包临时文件失败，已取消安装。"));
        return;
    }

    // 用户主动取消时不重试，直接清理临时文件。
    if (_downloadCancelled)
    {
        discardInstaller();
        closeDownloadProgress();
        return;
    }

    if (networkError != QNetworkReply::NoError)
    {
        qWarning().noquote() << "Update installer download failed:" << networkError << "HTTP" << httpStatus;

        // 服务器拒绝续传范围：删除 .part 后从 0 重下，并计入重试次数。
        if (httpStatus == 416)
        {
            QFile::remove(_installerPartPath);
            _resumeOffset = 0;
            if (scheduleInstallerRetry())
            {
                return;
            }
        }
        else
        {
            // 连接层错误（无 HTTP 状态码）或服务端临时故障才退避重试。
            const bool retryable = (httpStatus == 0) || isRetryableDownloadStatus(httpStatus);
            if (retryable && scheduleInstallerRetry())
            {
                return;
            }
        }

        discardInstaller();
        closeDownloadProgress();
        showUpdateError(QStringLiteral("更新安装包下载失败。\n%1").arg(failureReason));
        return;
    }

    if (!_installerWriteModeResolved && !resolveInstallerResponseMode(httpStatus))
    {
        discardInstaller();
        closeDownloadProgress();
        showUpdateError(QStringLiteral("更新安装包下载响应无效（HTTP %1）。").arg(httpStatus));
        return;
    }

    QFile installer(_installerPartPath);
    if (!installer.open(QIODevice::ReadOnly))
    {
        discardInstaller();
        closeDownloadProgress();
        showUpdateError(QStringLiteral("无法读取已下载的更新安装包。"));
        return;
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!installer.atEnd())
    {
        hash.addData(installer.read(1024 * 1024));
    }
    installer.close();

    if (hash.result().toHex().toLower() != _expectedChecksum)
    {
        qWarning() << "Update installer checksum verification failed";
        discardInstaller();
        closeDownloadProgress();
        showUpdateError(QStringLiteral("更新安装包校验失败，已取消安装。"));
        return;
    }

    // 校验通过后才把 .part 重命名为最终安装包路径，失败则保留原状。
    QFile::remove(_downloadedInstallerPath);
    if (!QFile::rename(_installerPartPath, _downloadedInstallerPath))
    {
        discardInstaller();
        closeDownloadProgress();
        showUpdateError(QStringLiteral("无法保存已下载的更新安装包。"));
        return;
    }

    resetDownloadRetry();
    closeDownloadProgress();
    installAndRestart();
}

/*****************************************************
函数名称：void UpdateManager::showUpdateConfirmation()
入口参数：无
出口参数：无
函数功能：询问用户立即更新、跳过此版本或稍后提醒
*****************************************************/
void UpdateManager::showUpdateConfirmation()
{
    const QString title = QStringLiteral("发现新版本");
    const QString message = QStringLiteral("当前版本：v%1\n最新版本：v%2\n\n是否立即下载并安装更新？")
                                .arg(QStringLiteral(COMMUNICATION_ANALYZER_VERSION),
                                     _availableVersion.toString());
    QMessageBox messageBox(QMessageBox::Question, title, message, QMessageBox::NoButton, _parentWindow);
    QPushButton* installButton = messageBox.addButton(QStringLiteral("立即更新"), QMessageBox::AcceptRole);
    QPushButton* skipButton = messageBox.addButton(QStringLiteral("跳过此版本"), QMessageBox::DestructiveRole);
    messageBox.addButton(QStringLiteral("稍后提醒"), QMessageBox::RejectRole);
    messageBox.setDefaultButton(installButton);
    messageBox.exec();

    if (messageBox.clickedButton() == installButton)
    {
        // 镜像清单已直接下发校验值时跳过校验文件下载，GitHub 路径行为不变。
        if (!_expectedChecksum.isEmpty())
        {
            requestInstaller(_installerUrl);
        }
        else
        {
            requestChecksum(_checksumUrl);
        }
    }
    else if (messageBox.clickedButton() == skipButton)
    {
        QSettings settings(QString::fromLatin1(kSettingsName), QString::fromLatin1(kSettingsName));
        settings.setValue(QString::fromLatin1(kSkippedVersionKey), _availableVersion.toString());
    }
}

/*****************************************************
函数名称：void UpdateManager::showDownloadProgress()
入口参数：无
出口参数：无
函数功能：显示更新安装包下载进度对话框
*****************************************************/
void UpdateManager::showDownloadProgress()
{
    if (_downloadProgressDialog == nullptr)
    {
        _downloadProgressDialog = new QProgressDialog(_parentWindow);
        _downloadProgressDialog->setWindowTitle(QStringLiteral("正在下载更新"));
        _downloadProgressDialog->setWindowModality(Qt::ApplicationModal);
        _downloadProgressDialog->setAutoClose(false);
        _downloadProgressDialog->setAutoReset(false);
        _downloadProgressDialog->setMinimumDuration(0);
        _downloadProgressDialog->setCancelButtonText(QStringLiteral("取消下载"));
        connect(_downloadProgressDialog, &QProgressDialog::canceled,
                this, &UpdateManager::cancelDownload);
    }

    _downloadProgressDialog->setLabelText(
        QStringLiteral("正在下载 v%1 更新安装包…").arg(_availableVersion.toString()));
    _downloadProgressDialog->setRange(0, 0);
    _downloadProgressDialog->setValue(0);
    _downloadProgressDialog->show();
}

/*****************************************************
函数名称：void UpdateManager::updateDownloadProgress(qint64 receivedBytes, qint64 totalBytes)
入口参数：receivedBytes为已下载字节数，totalBytes为总字节数
出口参数：无
函数功能：按续传起点加本次进度更新进度条百分比和提示文本
*****************************************************/
void UpdateManager::updateDownloadProgress(qint64 receivedBytes, qint64 totalBytes)
{
    if (_downloadProgressDialog == nullptr || totalBytes <= 0)
    {
        return;
    }

    // 续传时本次响应只包含剩余字节，需要叠加续传起点才能得到整体进度。
    const qint64 receivedTotal = _resumeOffset + receivedBytes;
    const qint64 byteTotal = _resumeOffset + totalBytes;
    const int percentage = static_cast<int>((receivedTotal * 100) / byteTotal);
    _downloadProgressDialog->setRange(0, 100);
    _downloadProgressDialog->setValue(percentage);
    _downloadProgressDialog->setLabelText(
        QStringLiteral("正在下载 v%1 更新安装包：%2%").arg(_availableVersion.toString()).arg(percentage));
}

/*****************************************************
函数名称：void UpdateManager::closeDownloadProgress()
入口参数：无
出口参数：无
函数功能：隐藏当前更新安装包下载进度对话框
*****************************************************/
void UpdateManager::closeDownloadProgress()
{
    if (_downloadProgressDialog != nullptr)
    {
        _downloadProgressDialog->hide();
    }
}

/*****************************************************
函数名称：void UpdateManager::showUpdateError(const QString& message)
入口参数：message为需要显示的更新失败原因
出口参数：无
函数功能：向用户显示可见的更新失败信息
*****************************************************/
void UpdateManager::showUpdateError(const QString& message)
{
    qWarning() << message;
    QMessageBox::warning(_parentWindow, QStringLiteral("更新失败"), message);
}

/*****************************************************
函数名称：void UpdateManager::cancelDownload()
入口参数：无
出口参数：无
函数功能：响应用户取消操作，作废待执行的重试并中止当前安装包下载
*****************************************************/
void UpdateManager::cancelDownload()
{
    _downloadCancelled = true;
    resetDownloadRetry();
    if (_installerReply != nullptr)
    {
        _installerReply->abort();
        return;
    }

    discardInstaller();
    closeDownloadProgress();
}

/*****************************************************
函数名称：bool UpdateManager::showPreviousUpdateFailure()
入口参数：无
出口参数：返回是否读取到上一次更新安装失败信息
函数功能：读取独立安装脚本写入的失败结果，并向用户显示原因
*****************************************************/
bool UpdateManager::showPreviousUpdateFailure()
{
    const QString resultPath = updateResultPath();
    QFile resultFile(resultPath);
    if (!resultFile.exists() || !resultFile.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        return false;
    }

    const QString failureMessage = QString::fromUtf8(resultFile.readAll()).trimmed();
    resultFile.close();
    QFile::remove(resultPath);
    if (failureMessage.isEmpty())
    {
        return false;
    }

    showUpdateError(QStringLiteral("上次更新未能完成。\n%1").arg(failureMessage));
    return true;
}

/*****************************************************
函数名称：void UpdateManager::installAndRestart()
入口参数：无
出口参数：无
函数功能：启动独立脚本，在本进程退出后安装 MSI 并重新启动程序
*****************************************************/
void UpdateManager::installAndRestart()
{
    const QString scriptPath = updateScriptPath();
    const QString executablePath = QCoreApplication::applicationFilePath();
    const QString resultPath = updateResultPath();
    const QStringList arguments{
        QStringLiteral("-NoProfile"),
        QStringLiteral("-NonInteractive"),
        QStringLiteral("-ExecutionPolicy"),
        QStringLiteral("Bypass"),
        QStringLiteral("-WindowStyle"),
        QStringLiteral("Hidden"),
        QStringLiteral("-File"),
        scriptPath,
        QStringLiteral("-ProcessId"),
        QString::number(QCoreApplication::applicationPid()),
        QStringLiteral("-InstallerPath"),
        _downloadedInstallerPath,
        QStringLiteral("-ExecutablePath"),
        executablePath,
        QStringLiteral("-ResultPath"),
        resultPath,
    };

    if (!QProcess::startDetached(QStringLiteral("powershell.exe"), arguments))
    {
        qWarning() << "Failed to launch the update installer";
        discardInstaller();
        showUpdateError(QStringLiteral("无法启动更新安装程序。"));
        return;
    }

    _installationStarted = true;
    QCoreApplication::quit();
}

/*****************************************************
函数名称：void UpdateManager::discardInstaller()
入口参数：无
出口参数：无
函数功能：关闭并删除临时 .part 文件及已下载的 MSI 安装包，并复位重试状态
*****************************************************/
void UpdateManager::discardInstaller()
{
    if (_installerFile.isOpen())
    {
        _installerFile.close();
    }
    if (!_downloadedInstallerPath.isEmpty())
    {
        QFile::remove(_downloadedInstallerPath);
    }
    if (!_installerPartPath.isEmpty())
    {
        QFile::remove(_installerPartPath);
    }
    resetDownloadRetry();
}

/*****************************************************
函数名称：QString UpdateManager::updateScriptPath() const
入口参数：无
出口参数：MSI 安装目录内的更新脚本完整路径
函数功能：定位随应用程序安装的独立更新脚本
*****************************************************/
QString UpdateManager::updateScriptPath() const
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("InstallUpdate.ps1"));
}

/*****************************************************
函数名称：QString UpdateManager::updateResultPath() const
入口参数：无
出口参数：独立安装脚本回传更新结果的文件路径
函数功能：生成与当前应用配置目录一致的更新结果文件路径
*****************************************************/
QString UpdateManager::updateResultPath() const
{
    const QString dataDirectory = QStandardPaths::writableLocation(
        QStandardPaths::AppLocalDataLocation);
    return QDir(dataDirectory).filePath(QStringLiteral("update-result.txt"));
}
