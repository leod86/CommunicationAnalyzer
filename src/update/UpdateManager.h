#ifndef UPDATEMANAGER_H
#define UPDATEMANAGER_H

#include <QObject>
#include <QFile>
#include <QJsonArray>
#include <QNetworkRequest>
#include <QUrl>
#include <QVersionNumber>

class QNetworkAccessManager;
class QNetworkReply;

/* 更新管理器：仅在 MSI 安装目录内检查 GitHub 正式版并执行升级。 */
class UpdateManager final : public QObject
{
public:
    // 创建应用程序更新管理器。
    explicit UpdateManager(QObject* parent = nullptr);
    // 释放更新管理器和未完成的下载文件。
    ~UpdateManager() override;

    // 在程序启动后异步检查并安装新版本。
    void checkForUpdates();

private:
    // 创建访问 GitHub API 或发布资源的网络请求。
    QNetworkRequest createRequest(const QUrl& url) const;
    // 获取最新正式 Release 的元数据。
    void requestLatestRelease();
    // 从 Release 资源清单中查找指定文件的下载地址。
    QUrl findAssetUrl(const QJsonArray& assets, const QString& assetName) const;
    // 下载安装包 SHA-256 校验文件。
    void requestChecksum(const QUrl& checksumUrl);
    // 下载 MSI 安装包并实时计算校验值。
    void requestInstaller(const QUrl& installerUrl);
    // 处理最新 Release 元数据响应。
    void handleLatestReleaseReply(QNetworkReply* reply);
    // 处理 SHA-256 校验文件响应。
    void handleChecksumReply(QNetworkReply* reply);
    // 处理 MSI 下载数据。
    void handleInstallerData(QNetworkReply* reply);
    // 处理 MSI 下载结束事件。
    void handleInstallerFinished(QNetworkReply* reply);
    // 启动独立更新脚本，等待当前程序退出后安装并重启。
    void installAndRestart();
    // 清理未完成或校验失败的安装包。
    void discardInstaller();
    // 返回随 MSI 一起部署的更新脚本路径。
    QString updateScriptPath() const;

    QNetworkAccessManager* _networkManager{nullptr};
    QFile _installerFile;
    QVersionNumber _availableVersion;
    QUrl _installerUrl;
    QString _downloadedInstallerPath;
    QByteArray _expectedChecksum;
    bool _checkStarted{false};
    bool _installationStarted{false};
};

#endif // UPDATEMANAGER_H
