# 发布与更新流程

## 分支约定

1. 所有功能修改从 `dev` 分支开始。
2. 推送 `dev` 会执行 `.github/workflows/verify-dev.yml` 的 Windows Release 构建验证。
3. 验证通过后创建 `dev` 到 `main` 的 Pull Request 并合并。
4. 仅 `main` 的推送会执行 `.github/workflows/release.yml`，构建 MSI 并创建 GitHub Release。

请在 GitHub 的 `Settings -> Branches` 为 `main` 设置保护规则：要求 Pull Request、要求
`Verify development branch / Build Windows release` 状态检查通过，并限制直接推送。

## 版本与安装包

Release 工作流使用 `CMakeLists.txt` 中定义的应用程序版本。首次正式版为 `v1.1.1`；后续每次
发布前，在 `dev` 分支递增 `project(CommunicationAnalyzer VERSION x.y.z)` 中的版本号。

- `CommunicationAnalyzer-x64.msi`
- `CommunicationAnalyzer-x64.msi.sha256`

MSI 由 CPack 和 WiX Toolset v3 生成，并使用固定 Upgrade GUID。因此高版本 MSI 会覆盖旧版
安装，而不会创建并列安装项。

## 启动更新

仅从 MSI 安装目录运行的应用会自动检查更新。更新管理器调用 GitHub 最新稳定 Release API，
比较版本后下载 MSI 和 SHA-256 文件；校验通过才会退出当前进程并启动独立安装脚本。脚本等待
应用退出，调用 Windows Installer 完成升级，再启动新版本。每台设备安装的 MSI 需要用户接受
Windows 的 UAC 授权请求。
