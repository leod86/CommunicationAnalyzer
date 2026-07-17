#ifndef ABOUTPAGE_H
#define ABOUTPAGE_H

#include "ElaScrollPage.h"

class ElaWindow;

class AboutPage final : public ElaScrollPage
{
    Q_OBJECT

public:
    // 创建用于显示软件信息和当前版本的关于页面。
    explicit AboutPage(ElaWindow* window);
    // 释放关于页面资源。
    ~AboutPage() override;

private:
    // 创建软件名称、版本号和发布说明区域。
    void buildUi();
};

#endif // ABOUTPAGE_H
