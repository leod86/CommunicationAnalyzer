#include "pages/AboutPage.h"

#include "ElaScrollPageArea.h"
#include "ElaText.h"
#include "ElaWindow.h"

#include <QCoreApplication>
#include <QVBoxLayout>
#include <QWidget>

/*****************************************************
函数名称：AboutPage::AboutPage(ElaWindow* window)
入口参数：window为应用程序主窗口
出口参数：无
函数功能：创建关于页面并显示当前软件版本
*****************************************************/
AboutPage::AboutPage(ElaWindow* window)
    : ElaScrollPage(window)
{
    setWindowTitle(QStringLiteral("关于"));
    setContentsMargins(20, 5, 20, 0);
    buildUi();
}

/*****************************************************
函数名称：AboutPage::~AboutPage()
入口参数：无
出口参数：无
函数功能：释放关于页面资源
*****************************************************/
AboutPage::~AboutPage() = default;

/*****************************************************
函数名称：void AboutPage::buildUi()
入口参数：无
出口参数：无
函数功能：创建软件名称、版本号和更新说明的展示区域
*****************************************************/
void AboutPage::buildUi()
{
    auto* centralWidget = new QWidget(this);
    centralWidget->setWindowTitle(QStringLiteral("关于"));
    auto* centralLayout = new QVBoxLayout(centralWidget);
    centralLayout->setContentsMargins(0, 20, 0, 20);
    centralLayout->setSpacing(10);

    auto* title = new ElaText(QStringLiteral("通讯协议分析仪"), centralWidget);
    title->setTextPixelSize(22);
    centralLayout->addWidget(title);

    auto* informationArea = new ElaScrollPageArea(centralWidget);
    auto* informationLayout = new QVBoxLayout(informationArea);
    informationLayout->setContentsMargins(20, 16, 20, 16);
    informationLayout->setSpacing(8);

    auto* versionText = new ElaText(
        QStringLiteral("当前版本：v%1").arg(QCoreApplication::applicationVersion()), informationArea);
    versionText->setTextPixelSize(16);
    informationLayout->addWidget(versionText);

    auto* updateText = new ElaText(
        QStringLiteral("程序启动时会检查 GitHub 正式版本，确认后下载、安装并自动重启。"), informationArea);
    updateText->setTextPixelSize(14);
    updateText->setWordWrap(true);
    informationLayout->addWidget(updateText);

    centralLayout->addWidget(informationArea);
    centralLayout->addStretch();
    addCentralWidget(centralWidget, true, true, 0);
}
