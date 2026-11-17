#ifndef UPDATEMANAGER_H
#define UPDATEMANAGER_H

#include <QObject>
#include <QFile>
#include <QJsonArray>
#include <QNetworkRequest>
#include <QPointer>
#include <QUrl>
#include <QVersionNumber>

class QNetworkAccessManager;
class QNetworkReply;
class QProgressDialog;
class QWidget;

/* 更新管理器：仅在 MSI 安装目录内检查 GitHub 正式版并执行升级。 */
class UpdateManager final : public QObject
{
public:
    // 创建应用程序更新管理器。
    explicit UpdateManager(QWidget* parentWindow, QObject* parent = nullptr);
    // 释放更新管理器和未完成的下载文件。
    ~UpdateManager() override;

    // 在程序启动后异步检查并安装新版本。
    void checkForUpdates();

private:
    // 创建访问 GitHub API 或发布资源的网络请求。
    QNetworkRequest createRequest(const QUrl& url) const;
    // 获取 R2 镜像上的 latest.json 更新清单。
    void requestMirrorManifest();
    // 返回镜像清单 latest.json 的完整地址。
    QUrl mirrorManifestUrl() const;
    // 处理镜像清单响应，失败时回退到 GitHub Release API。
    void handleMirrorManifestReply(QNetworkReply* reply);
    // 获取最新正式 Release 的元数据。
    void requestLatestRelease();
    // 应用已确认的新版本信息并弹出更新确认框，版本不新或已跳过时返回 false。
    bool applyAvailableRelease(const QVersionNumber& version, const QUrl& installerUrl,
                               const QUrl& checksumUrl, const QByteArray& expectedChecksum);
    // 从 Release 资源清单中查找指定文件的下载地址。
    QUrl findAssetUrl(const QJsonArray& assets, const QString& assetName) const;
    // 下载安装包 SHA-256 校验文件。
    void requestChecksum(const QUrl& checksumUrl);
    // 下载 MSI 安装包并实时计算校验值。
    void requestInstaller(const QUrl& installerUrl);
    // 开始或续传一次安装包下载请求。
    void startInstallerDownload();
    // 按响应状态码决定追加写入还是从零重写，失败返回 false。
    bool resolveInstallerResponseMode(int httpStatus);
    // 安排一次退避重试，重试次数用尽时返回 false。
    bool scheduleInstallerRetry();
    // 复位断点续传与重试状态，并让待执行的重试失效。
    void resetDownloadRetry();
    // 处理最新 Release 元数据响应。
    void handleLatestReleaseReply(QNetworkReply* reply);
    // 处理 SHA-256 校验文件响应。
    void handleChecksumReply(QNetworkReply* reply);
    // 处理 MSI 下载数据。
    void handleInstallerData(QNetworkReply* reply);
    // 处理 MSI 下载结束事件。
    void handleInstallerFinished(QNetworkReply* reply);
    // 显示新版本确认对话框，确认后再下载更新。
    void showUpdateConfirmation();
    // 创建并显示安装包下载进度对话框。
    void showDownloadProgress();
    // 按下载字节数刷新进度条和提示文字。
    void updateDownloadProgress(qint64 receivedBytes, qint64 totalBytes);
    // 关闭下载进度对话框。
    void closeDownloadProgress();
    // 显示更新检查、下载或校验失败的原因。
    void showUpdateError(const QString& message);
    // 中止当前安装包下载并清理临时文件。
    void cancelDownload();
    // 显示独立安装脚本回传的上次更新失败原因。
    bool showPreviousUpdateFailure();
    // 启动独立更新脚本，等待当前程序退出后安装并重启。
    void installAndRestart();
    // 清理未完成或校验失败的安装包。
    void discardInstaller();
    // 返回随 MSI 一起部署的更新脚本路径。
    QString updateScriptPath() const;
    // 返回独立安装脚本写入更新结果的临时状态文件路径。
    QString updateResultPath() const;

    QPointer<QWidget> _parentWindow;
    QNetworkAccessManager* _networkManager{nullptr};
    QProgressDialog* _downloadProgressDialog{nullptr};
    QPointer<QNetworkReply> _installerReply;
    QFile _installerFile;
    // 下载中的 .part 临时文件路径，校验通过后才重命名为最终安装包。
    QString _installerPartPath;
    // 本次请求的续传起点字节数，响应 200 时会归零。
    qint64 _resumeOffset{0};
    // 已执行的重试次数，最多 kMaxDownloadRetries 次。
    int _downloadRetryCount{0};
    // 重试令牌，取消或重新开始时递增以作废待执行的重试。
    int _retryToken{0};
    // 本次响应是追加写入还是从头写入是否已确定。
    bool _installerWriteModeResolved{false};
    // 写入临时文件是否已失败，失败后不再重试。
    bool _installerWriteFailed{false};
    QVersionNumber _availableVersion;
    QUrl _installerUrl;
    QUrl _checksumUrl;
    QString _downloadedInstallerPath;
    QByteArray _expectedChecksum;
    bool _checkStarted{false};
    bool _downloadCancelled{false};
    bool _installationStarted{false};
};

#endif // UPDATEMANAGER_H
