#include "pages/SettingPage.h"

#include "ElaApplication.h"
#include "ElaComboBox.h"
#include "ElaScrollPageArea.h"
#include "ElaText.h"
#include "ElaTheme.h"
#include "ElaWindow.h"

#include <QHBoxLayout>
#include <QSettings>
#include <QSignalBlocker>
#include <QVBoxLayout>

/*****************************************************
函数名称：SettingPage::SettingPage(ElaWindow* window)
入口参数：window为应用程序主窗口
出口参数：无
函数功能：创建并初始化应用程序设置页面
*****************************************************/
SettingPage::SettingPage(ElaWindow* window)
    : ElaScrollPage(window)
    , _window(window)
{
    setWindowTitle(QStringLiteral("设置"));
    setContentsMargins(20, 5, 20, 0);
    buildUi();
}

/*****************************************************
函数名称：SettingPage::~SettingPage()
入口参数：无
出口参数：无
函数功能：释放应用程序设置页面
*****************************************************/
SettingPage::~SettingPage() = default;

/*****************************************************
函数名称：void SettingPage::buildUi()
入口参数：无
出口参数：无
函数功能：使用Ela页面区域创建主题、导航栏和页面切换设置项
*****************************************************/
void SettingPage::buildUi()
{
    auto* centralWidget = new QWidget(this);
    centralWidget->setWindowTitle(QStringLiteral("设置"));
    auto* centralLayout = new QVBoxLayout(centralWidget);
    centralLayout->setContentsMargins(0, 20, 0, 20);
    centralLayout->setSpacing(10);

    auto* appearanceTitle = new ElaText(QStringLiteral("外观设置"), centralWidget);
    appearanceTitle->setTextPixelSize(18);
    centralLayout->addWidget(appearanceTitle);

    auto* themeArea = new ElaScrollPageArea(centralWidget);
    auto* themeLayout = new QHBoxLayout(themeArea);
    auto* themeText = new ElaText(QStringLiteral("主题模式"), themeArea);
    themeText->setTextPixelSize(15);
    _themeComboBox = new ElaComboBox(themeArea);
    _themeComboBox->addItem(QStringLiteral("浅色"), static_cast<int>(ElaThemeType::Light));
    _themeComboBox->addItem(QStringLiteral("深色"), static_cast<int>(ElaThemeType::Dark));
    _themeComboBox->setFixedWidth(130);
    themeLayout->addWidget(themeText);
    themeLayout->addStretch();
    themeLayout->addWidget(_themeComboBox);
    centralLayout->addWidget(themeArea);

    auto* windowDisplayArea = new ElaScrollPageArea(centralWidget);
    auto* windowDisplayLayout = new QHBoxLayout(windowDisplayArea);
    auto* windowDisplayText = new ElaText(QStringLiteral("窗口效果"), windowDisplayArea);
    windowDisplayText->setTextPixelSize(15);
    _windowDisplayModeComboBox = new ElaComboBox(windowDisplayArea);
    _windowDisplayModeComboBox->addItem(QStringLiteral("Normal"), static_cast<int>(ElaApplicationType::Normal));
    _windowDisplayModeComboBox->addItem(QStringLiteral("ElaMica"), static_cast<int>(ElaApplicationType::ElaMica));
#ifdef Q_OS_WIN
    _windowDisplayModeComboBox->addItem(QStringLiteral("Mica"), static_cast<int>(ElaApplicationType::Mica));
    _windowDisplayModeComboBox->addItem(QStringLiteral("Mica-Alt"), static_cast<int>(ElaApplicationType::MicaAlt));
    _windowDisplayModeComboBox->addItem(QStringLiteral("Acrylic"), static_cast<int>(ElaApplicationType::Acrylic));
    _windowDisplayModeComboBox->addItem(QStringLiteral("Dwm-Blur"), static_cast<int>(ElaApplicationType::DWMBlur));
#endif
    _windowDisplayModeComboBox->setFixedWidth(130);
    windowDisplayLayout->addWidget(windowDisplayText);
    windowDisplayLayout->addStretch();
    windowDisplayLayout->addWidget(_windowDisplayModeComboBox);
    centralLayout->addWidget(windowDisplayArea);

    auto* navigationArea = new ElaScrollPageArea(centralWidget);
    auto* navigationLayout = new QHBoxLayout(navigationArea);
    auto* navigationText = new ElaText(QStringLiteral("左侧导航栏模式"), navigationArea);
    navigationText->setTextPixelSize(15);
    _navigationModeComboBox = new ElaComboBox(navigationArea);
    _navigationModeComboBox->addItem(QStringLiteral("自动"), static_cast<int>(ElaNavigationType::Auto));
    _navigationModeComboBox->addItem(QStringLiteral("最小"), static_cast<int>(ElaNavigationType::Minimal));
    _navigationModeComboBox->addItem(QStringLiteral("紧凑"), static_cast<int>(ElaNavigationType::Compact));
    _navigationModeComboBox->addItem(QStringLiteral("展开"), static_cast<int>(ElaNavigationType::Maximal));
    _navigationModeComboBox->setFixedWidth(130);
    navigationLayout->addWidget(navigationText);
    navigationLayout->addStretch();
    navigationLayout->addWidget(_navigationModeComboBox);
    centralLayout->addWidget(navigationArea);

    auto* stackSwitchArea = new ElaScrollPageArea(centralWidget);
    auto* stackSwitchLayout = new QHBoxLayout(stackSwitchArea);
    auto* stackSwitchText = new ElaText(QStringLiteral("页面切换动画"), stackSwitchArea);
    stackSwitchText->setTextPixelSize(15);
    _stackSwitchModeComboBox = new ElaComboBox(stackSwitchArea);
    _stackSwitchModeComboBox->addItem(QStringLiteral("无"), static_cast<int>(ElaWindowType::None));
    _stackSwitchModeComboBox->addItem(QStringLiteral("弹出"), static_cast<int>(ElaWindowType::Popup));
    _stackSwitchModeComboBox->addItem(QStringLiteral("缩放"), static_cast<int>(ElaWindowType::Scale));
    _stackSwitchModeComboBox->addItem(QStringLiteral("翻转"), static_cast<int>(ElaWindowType::Flip));
    _stackSwitchModeComboBox->addItem(QStringLiteral("模糊"), static_cast<int>(ElaWindowType::Blur));
    _stackSwitchModeComboBox->setFixedWidth(130);
    stackSwitchLayout->addWidget(stackSwitchText);
    stackSwitchLayout->addStretch();
    stackSwitchLayout->addWidget(_stackSwitchModeComboBox);
    centralLayout->addWidget(stackSwitchArea);
    centralLayout->addStretch();

    connect(_themeComboBox, qOverload<int>(&ElaComboBox::currentIndexChanged), this, [this](int) {
        const auto themeMode = static_cast<ElaThemeType::ThemeMode>(_themeComboBox->currentData().toInt());
        eTheme->setThemeMode(themeMode);
        saveSetting(QStringLiteral("appearance/theme"), _themeComboBox->currentData());
    });
    connect(_windowDisplayModeComboBox, qOverload<int>(&ElaComboBox::currentIndexChanged), this, [this](int) {
        const auto windowDisplayMode = static_cast<ElaApplicationType::WindowDisplayMode>(
            _windowDisplayModeComboBox->currentData().toInt());
        eApp->setWindowDisplayMode(windowDisplayMode);
        saveSetting(QStringLiteral("appearance/windowDisplayMode"), _windowDisplayModeComboBox->currentData());
    });
    connect(_navigationModeComboBox, qOverload<int>(&ElaComboBox::currentIndexChanged), this, [this](int) {
        const auto navigationMode = static_cast<ElaNavigationType::NavigationDisplayMode>(
            _navigationModeComboBox->currentData().toInt());
        _window->setNavigationBarDisplayMode(navigationMode);
        saveSetting(QStringLiteral("appearance/navigationMode"), _navigationModeComboBox->currentData());
    });
    connect(_stackSwitchModeComboBox, qOverload<int>(&ElaComboBox::currentIndexChanged), this, [this](int) {
        const auto stackSwitchMode = static_cast<ElaWindowType::StackSwitchMode>(
            _stackSwitchModeComboBox->currentData().toInt());
        _window->setStackSwitchMode(stackSwitchMode);
        saveSetting(QStringLiteral("appearance/stackSwitchMode"), _stackSwitchModeComboBox->currentData());
    });
    connect(eTheme, &ElaTheme::themeModeChanged, this, [this](ElaThemeType::ThemeMode themeMode) {
        const QSignalBlocker blocker(_themeComboBox);
        const int index = _themeComboBox->findData(static_cast<int>(themeMode));
        if (index >= 0)
        {
            _themeComboBox->setCurrentIndex(index);
        }
    });
    connect(eApp, &ElaApplication::pWindowDisplayModeChanged, this, [this]() {
        const QSignalBlocker blocker(_windowDisplayModeComboBox);
        const int index = _windowDisplayModeComboBox->findData(static_cast<int>(eApp->getWindowDisplayMode()));
        if (index >= 0)
        {
            _windowDisplayModeComboBox->setCurrentIndex(index);
        }
    });

    addCentralWidget(centralWidget, true, true, 0);
}

/*****************************************************
函数名称：void SettingPage::applySavedSettings()
入口参数：无
出口参数：无
函数功能：读取本地外观配置并同步到Ela主题和主窗口
*****************************************************/
void SettingPage::applySavedSettings()
{
    QSettings settings(QStringLiteral("CommunicationAnalyzer"), QStringLiteral("CommunicationAnalyzer"));
    const int themeValue = settings.value(
        QStringLiteral("appearance/theme"), static_cast<int>(ElaThemeType::Light)).toInt();
    const int windowDisplayValue = settings.value(
        QStringLiteral("appearance/windowDisplayMode"), static_cast<int>(ElaApplicationType::Normal)).toInt();
    const int navigationValue = settings.value(
        QStringLiteral("appearance/navigationMode"), static_cast<int>(ElaNavigationType::Auto)).toInt();
    const int stackSwitchValue = settings.value(
        QStringLiteral("appearance/stackSwitchMode"), static_cast<int>(ElaWindowType::Popup)).toInt();

    const auto restoreComboBox = [](ElaComboBox* comboBox, int value) {
        const QSignalBlocker blocker(comboBox);
        const int index = comboBox->findData(value);
        if (index >= 0)
        {
            comboBox->setCurrentIndex(index);
        }
    };
    restoreComboBox(_themeComboBox, themeValue);
    restoreComboBox(_windowDisplayModeComboBox, windowDisplayValue);
    restoreComboBox(_navigationModeComboBox, navigationValue);
    restoreComboBox(_stackSwitchModeComboBox, stackSwitchValue);

    eTheme->setThemeMode(static_cast<ElaThemeType::ThemeMode>(_themeComboBox->currentData().toInt()));
    eApp->setWindowDisplayMode(static_cast<ElaApplicationType::WindowDisplayMode>(
        _windowDisplayModeComboBox->currentData().toInt()));
    _window->setNavigationBarDisplayMode(static_cast<ElaNavigationType::NavigationDisplayMode>(
        _navigationModeComboBox->currentData().toInt()));
    _window->setStackSwitchMode(static_cast<ElaWindowType::StackSwitchMode>(
        _stackSwitchModeComboBox->currentData().toInt()));
}

/*****************************************************
函数名称：void SettingPage::saveSetting(const QString& key, const QVariant& value) const
入口参数：key为配置键，value为待保存值
出口参数：无
函数功能：将单项外观设置写入本地配置
*****************************************************/
void SettingPage::saveSetting(const QString& key, const QVariant& value) const
{
    QSettings settings(QStringLiteral("CommunicationAnalyzer"), QStringLiteral("CommunicationAnalyzer"));
    settings.setValue(key, value);
}
