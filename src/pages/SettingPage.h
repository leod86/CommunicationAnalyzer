#ifndef SETTINGPAGE_H
#define SETTINGPAGE_H

#include "ElaScrollPage.h"

class ElaComboBox;
class ElaText;
class ElaToggleSwitch;
class ElaWindow;
class QVariant;

class SettingPage final : public ElaScrollPage
{
    Q_OBJECT

public:
    // 创建应用程序设置页面。
    explicit SettingPage(ElaWindow* window);
    // 释放应用程序设置页面。
    ~SettingPage() override;
    // 从本地配置恢复并应用外观设置。
    void applySavedSettings();
    // 获取串口 MCP 服务开关状态。
    bool mcpEnabled() const;
    // 同步串口 MCP 服务开关状态。
    void setMcpEnabled(bool enabled);

signals:
    // 通知主窗口启动或停止串口 MCP 服务。
    void mcpEnabledChanged(bool enabled);

public slots:
    // 更新串口 MCP 服务运行状态。
    void setMcpServiceState(bool running, const QString& message);

private:
    // 创建主题、导航栏和页面切换设置项。
    void buildUi();
    // 保存指定设置项的当前值。
    void saveSetting(const QString& key, const QVariant& value) const;

    ElaWindow* _window{nullptr};
    ElaComboBox* _themeComboBox{nullptr};
    ElaComboBox* _windowDisplayModeComboBox{nullptr};
    ElaComboBox* _navigationModeComboBox{nullptr};
    ElaComboBox* _stackSwitchModeComboBox{nullptr};
    ElaToggleSwitch* _mcpSwitch{nullptr};
    ElaText* _mcpStatusText{nullptr};
};

#endif // SETTINGPAGE_H
