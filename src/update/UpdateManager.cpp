#include "update/UpdateManager.h"

#include <QCoreApplication>
#include <QCryptographicHash>
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
#include <QRegularExpression>
#include <QStandardPaths>
#include <QWidget>
#include <QDebug>

namespace
{
constexpr auto kInstallerAssetName = "CommunicationAnalyzer-x64.msi";
constexpr auto kChecksumAssetName = "CommunicationAnalyzer-x64.msi.sha256";
}

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

    _checkStarted = true;
    requestLatestRelease();
}

/*****************************************************
函数名称：QNetworkRequest UpdateManager::createRequest(const QUrl& url) const
入口参数：url 为 GitHub API 或发布资源地址
出口参数：配置完成的网络请求
函数功能：设置 GitHub API 所需请求头和安全重定向策略
*****************************************************/
QNetworkRequest UpdateManager::createRequest(const QUrl& url) const
{
    QNetworkRequest request(url);
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("User-Agent", "CommunicationAnalyzer/" COMMUNICATION_ANALYZER_VERSION);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(60'000);
    return request;
}

/*****************************************************
函数名称：void UpdateManager::requestLatestRelease()
入口参数：无
出口参数：无
函数功能：请求 GitHub 最新正式 Release 的版本和资源列表
*****************************************************/
void UpdateManager::requestLatestRelease()
{
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
函数功能：创建临时 MSI 文件并开始异步下载
*****************************************************/
void UpdateManager::requestInstaller(const QUrl& installerUrl)
{
    const QString temporaryDirectory = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    if (temporaryDirectory.isEmpty())
    {
        return;
    }

    _downloadedInstallerPath = QDir(temporaryDirectory).filePath(
        QStringLiteral("CommunicationAnalyzer-%1-x64.msi").arg(_availableVersion.toString()));
    QFile::remove(_downloadedInstallerPath);
    _installerFile.setFileName(_downloadedInstallerPath);
    if (!_installerFile.open(QIODevice::WriteOnly))
    {
        discardInstaller();
        showUpdateError(QStringLiteral("无法创建更新安装包的临时文件。"));
        return;
    }

    _downloadCancelled = false;
    showDownloadProgress();
    QNetworkReply* reply = _networkManager->get(createRequest(installerUrl));
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
函数名称：void UpdateManager::handleLatestReleaseReply(QNetworkReply* reply)
入口参数：reply 为 GitHub 最新 Release 响应
出口参数：无
函数功能：比较线上与当前版本，并获取更新所需资源地址
*****************************************************/
void UpdateManager::handleLatestReleaseReply(QNetworkReply* reply)
{
    const QByteArray responseBody = reply->readAll();
    const QNetworkReply::NetworkError networkError = reply->error();
    const QString errorString = reply->errorString();
    reply->deleteLater();
    if (networkError != QNetworkReply::NoError)
    {
        qWarning() << "Update check failed:" << networkError;
        showUpdateError(QStringLiteral("无法检查更新：%1").arg(errorString));
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(responseBody, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        qWarning() << "Update release metadata is invalid";
        showUpdateError(QStringLiteral("更新信息格式无效。"));
        return;
    }

    const QJsonObject release = document.object();
    if (release.value(QStringLiteral("draft")).toBool()
        || release.value(QStringLiteral("prerelease")).toBool())
    {
        return;
    }

    QString versionText = release.value(QStringLiteral("tag_name")).toString();
    if (versionText.startsWith(QLatin1Char('v'), Qt::CaseInsensitive))
    {
        versionText.remove(0, 1);
    }
    qsizetype parsedLength = 0;
    const QVersionNumber releaseVersion = QVersionNumber::fromString(versionText, &parsedLength);
    const QVersionNumber currentVersion = QVersionNumber::fromString(
        QStringLiteral(COMMUNICATION_ANALYZER_VERSION));
    if (parsedLength != versionText.size() || releaseVersion.isNull()
        || currentVersion.isNull() || QVersionNumber::compare(releaseVersion, currentVersion) <= 0)
    {
        return;
    }

    const QJsonArray assets = release.value(QStringLiteral("assets")).toArray();
    const QUrl installerUrl = findAssetUrl(assets, QString::fromLatin1(kInstallerAssetName));
    const QUrl checksumUrl = findAssetUrl(assets, QString::fromLatin1(kChecksumAssetName));
    if (!installerUrl.isValid() || !checksumUrl.isValid())
    {
        qWarning() << "Update release does not contain the required MSI assets";
        showUpdateError(QStringLiteral("最新版本缺少安装包或校验文件。"));
        return;
    }

    _availableVersion = releaseVersion;
    _installerUrl = installerUrl;
    _checksumUrl = checksumUrl;
    showUpdateConfirmation();
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
    const QString errorString = reply->errorString();
    reply->deleteLater();
    if (networkError != QNetworkReply::NoError)
    {
        qWarning() << "Update checksum download failed:" << networkError;
        showUpdateError(QStringLiteral("无法下载更新校验文件：%1").arg(errorString));
        return;
    }

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
函数功能：将收到的 MSI 数据写入临时文件
*****************************************************/
void UpdateManager::handleInstallerData(QNetworkReply* reply)
{
    const QByteArray data = reply->readAll();
    if (_installerFile.write(data) != data.size())
    {
        reply->abort();
        discardInstaller();
    }
}

/*****************************************************
函数名称：void UpdateManager::handleInstallerFinished(QNetworkReply* reply)
入口参数：reply 为 MSI 下载响应
出口参数：无
函数功能：校验已下载 MSI 的 SHA-256，并在成功后执行更新
*****************************************************/
void UpdateManager::handleInstallerFinished(QNetworkReply* reply)
{
    handleInstallerData(reply);
    const QNetworkReply::NetworkError networkError = reply->error();
    const QString errorString = reply->errorString();
    reply->deleteLater();
    _installerReply = nullptr;
    _installerFile.close();
    if (networkError != QNetworkReply::NoError)
    {
        qWarning() << "Update installer download failed:" << networkError;
        discardInstaller();
        closeDownloadProgress();
        if (!_downloadCancelled)
        {
            showUpdateError(QStringLiteral("更新安装包下载失败：%1").arg(errorString));
        }
        return;
    }

    QFile installer(_downloadedInstallerPath);
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

    closeDownloadProgress();
    installAndRestart();
}

/*****************************************************
函数名称：void UpdateManager::showUpdateConfirmation()
入口参数：无
出口参数：无
函数功能：询问用户是否下载并安装检测到的新版本
*****************************************************/
void UpdateManager::showUpdateConfirmation()
{
    const QString title = QStringLiteral("发现新版本");
    const QString message = QStringLiteral("当前版本：v%1\n最新版本：v%2\n\n是否立即下载并安装更新？")
                                .arg(QStringLiteral(COMMUNICATION_ANALYZER_VERSION),
                                     _availableVersion.toString());
    const QMessageBox::StandardButton answer = QMessageBox::question(
        _parentWindow, title, message, QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
    if (answer == QMessageBox::Yes)
    {
        requestChecksum(_checksumUrl);
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
函数功能：根据网络下载进度更新进度条百分比和提示文本
*****************************************************/
void UpdateManager::updateDownloadProgress(qint64 receivedBytes, qint64 totalBytes)
{
    if (_downloadProgressDialog == nullptr || totalBytes <= 0)
    {
        return;
    }

    const int percentage = static_cast<int>((receivedBytes * 100) / totalBytes);
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
函数功能：响应用户取消操作并中止当前安装包下载
*****************************************************/
void UpdateManager::cancelDownload()
{
    _downloadCancelled = true;
    if (_installerReply != nullptr)
    {
        _installerReply->abort();
        return;
    }

    discardInstaller();
    closeDownloadProgress();
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
函数功能：关闭并删除临时 MSI 安装包
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
