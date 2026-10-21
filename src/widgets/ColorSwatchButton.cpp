#include "widgets/ColorSwatchButton.h"

#include <QEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPen>

/*****************************************************
函数名称：ColorSwatchButton::ColorSwatchButton(QWidget* parent)
入口参数：parent为父窗口
出口参数：无
函数功能：创建可点击的颜色块按钮并启用悬停刷新
*****************************************************/
ColorSwatchButton::ColorSwatchButton(QWidget* parent)
    : QAbstractButton(parent)
    , _color(Qt::white)
{
    setAttribute(Qt::WA_Hover, true);
    setCursor(Qt::PointingHandCursor);
    setFixedSize(26, 26);
    setToolTip(QStringLiteral("点击选择标记颜色"));
}

/*****************************************************
函数名称：ColorSwatchButton::~ColorSwatchButton()
入口参数：无
出口参数：无
函数功能：释放色块按钮
*****************************************************/
ColorSwatchButton::~ColorSwatchButton() = default;

/*****************************************************
函数名称：QColor ColorSwatchButton::color() const
入口参数：无
出口参数：当前颜色
函数功能：返回当前选中的标记颜色
*****************************************************/
QColor ColorSwatchButton::color() const
{
    return _color;
}

/*****************************************************
函数名称：void ColorSwatchButton::setColor(const QColor& color)
入口参数：color为目标颜色
出口参数：无
函数功能：更新色块颜色并刷新显示
*****************************************************/
void ColorSwatchButton::setColor(const QColor& color)
{
    if (!color.isValid() || color == _color)
    {
        return;
    }

    _color = color;
    update();
}

/*****************************************************
函数名称：bool ColorSwatchButton::event(QEvent* event)
入口参数：event为控件事件
出口参数：事件是否已处理
函数功能：鼠标进入或离开时刷新边框状态
*****************************************************/
bool ColorSwatchButton::event(QEvent* event)
{
    if (event->type() == QEvent::HoverEnter || event->type() == QEvent::HoverLeave)
    {
        update();
    }

    return QAbstractButton::event(event);
}

/*****************************************************
函数名称：void ColorSwatchButton::paintEvent(QPaintEvent* event)
入口参数：event为绘制事件
出口参数：无
函数功能：绘制圆角色块与随状态变化的边框
*****************************************************/
void ColorSwatchButton::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const QRectF rect = QRectF(this->rect()).adjusted(1.5, 1.5, -1.5, -1.5);

    painter.setPen(Qt::NoPen);
    painter.setBrush(_color);
    painter.drawRoundedRect(rect, 5, 5);

    QColor borderColor = palette().color(QPalette::Mid);
    if (isDown() || underMouse() || hasFocus())
    {
        borderColor = palette().color(QPalette::Highlight);
    }

    QPen borderPen(borderColor);
    borderPen.setWidthF(1.0);
    painter.setPen(borderPen);
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(rect, 5, 5);
}
