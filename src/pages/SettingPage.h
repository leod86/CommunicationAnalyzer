#ifndef SETTINGPAGE_H
#define SETTINGPAGE_H

#include "ElaScrollPage.h"

class ElaComboBox;
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
};

#endif // SETTINGPAGE_H
