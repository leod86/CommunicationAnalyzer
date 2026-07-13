#include "widgets/TrafficTextEdit.h"

#include <QPaintEvent>
#include <QPainter>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>
#include <QWheelEvent>

/*****************************************************
函数名称：TrafficTextEdit::TrafficTextEdit(QWidget* parent)
入口参数：parent为父窗口
出口参数：无
函数功能：初始化支持方向圆角标签的通讯文本控件
*****************************************************/
TrafficTextEdit::TrafficTextEdit(QWidget* parent)
    : ElaPlainTextEdit(parent)
{
}

/*****************************************************
函数名称：TrafficTextEdit::~TrafficTextEdit()
入口参数：无
出口参数：无
函数功能：释放通讯文本控件
*****************************************************/
TrafficTextEdit::~TrafficTextEdit() = default;

/*****************************************************
函数名称：void TrafficTextEdit::setTextPointSize(int pointSize)
入口参数：pointSize为目标字号
出口参数：无
函数功能：统一更新接收窗口和已有文档的默认字号
*****************************************************/
void TrafficTextEdit::setTextPointSize(int pointSize)
{
    const int boundedPointSize = qBound(7, pointSize, 32);
    QFont textFont = font();
    textFont.setPointSize(boundedPointSize);
    setFont(textFont);
    document()->setDefaultFont(textFont);
    viewport()->update();
}

/*****************************************************
函数名称：int TrafficTextEdit::textPointSize() const
入口参数：无
出口参数：当前接收窗口字号
函数功能：返回当前接收窗口使用的文字字号
*****************************************************/
int TrafficTextEdit::textPointSize() const
{
    const int pointSize = document()->defaultFont().pointSize();
    return pointSize > 0 ? pointSize : 10;
}

/*****************************************************
函数名称：void TrafficTextEdit::paintEvent(QPaintEvent* event)
入口参数：event为绘制事件
出口参数：无
函数功能：先绘制普通文本，再绘制RX/TX圆角徽标
*****************************************************/
void TrafficTextEdit::paintEvent(QPaintEvent* event)
{
    ElaPlainTextEdit::paintEvent(event);
    drawDirectionBadges();
}

/*****************************************************
函数名称：void TrafficTextEdit::wheelEvent(QWheelEvent* event)
入口参数：event为鼠标滚轮事件
出口参数：无
函数功能：接收窗口获得焦点时使用Ctrl加滚轮调整文字大小
*****************************************************/
void TrafficTextEdit::wheelEvent(QWheelEvent* event)
{
    if (hasFocus() && event->modifiers().testFlag(Qt::ControlModifier) && event->angleDelta().y() != 0)
    {
        const int previousPointSize = textPointSize();
        const int nextPointSize = previousPointSize + (event->angleDelta().y() > 0 ? 1 : -1);
        setTextPointSize(nextPointSize);
        const int currentPointSize = textPointSize();
        if (currentPointSize != previousPointSize)
        {
            emit textPointSizeChanged(currentPointSize);
        }
        event->accept();
        return;
    }

    ElaPlainTextEdit::wheelEvent(event);
}

/*****************************************************
函数名称：void TrafficTextEdit::drawDirectionBadges()
入口参数：无
出口参数：无
函数功能：在可见记录的方向文字位置绘制不同颜色圆角背景
*****************************************************/
void TrafficTextEdit::drawDirectionBadges()
{
    QPainter painter(viewport());
    painter.setRenderHint(QPainter::Antialiasing);

    const bool darkMode = palette().color(QPalette::Base).lightness() < 128;
    QTextBlock block = firstVisibleBlock();
    const QPointF offset = contentOffset();

    while (block.isValid())
    {
        const QRectF blockRect = blockBoundingGeometry(block).translated(offset);
        if (blockRect.top() > viewport()->height())
        {
            break;
        }

        if (blockRect.bottom() >= 0)
        {
            const QString blockText = block.text();
            static const QRegularExpression directionPattern(
                QStringLiteral("^(?:\\[\\d{2}:\\d{2}:\\d{2}\\.\\d{3}\\]\\s)?(\\[(?:RX|TX)\\])"));
            const QRegularExpressionMatch directionMatch = directionPattern.match(blockText);
            const int tagIndex = directionMatch.hasMatch() ? directionMatch.capturedStart(1) : -1;
            const QString tag = directionMatch.hasMatch() ? directionMatch.captured(1) : QString();
            const bool received = tag == QStringLiteral("[RX]");

            QTextLayout* textLayout = block.layout();
            if (tagIndex >= 0 && textLayout)
            {
                const QTextLine line = textLayout->lineForTextPosition(tagIndex);
                if (line.isValid())
                {
                    const qreal left = line.cursorToX(tagIndex);
                    const qreal right = line.cursorToX(tagIndex + tag.size());
                    QRectF badgeRect(blockRect.left() + left - 3,
                                     blockRect.top() + line.y() + 1,
                                     right - left + 6,
                                     line.height() - 2);

                    const QColor background = received
                        ? (darkMode ? QColor(QStringLiteral("#15803D")) : QColor(QStringLiteral("#22C55E")))
                        : (darkMode ? QColor(QStringLiteral("#1D4ED8")) : QColor(QStringLiteral("#3B82F6")));

                    painter.setPen(Qt::NoPen);
                    painter.setBrush(background);
                    painter.drawRoundedRect(badgeRect, 5, 5);

                    QFont badgeFont = font();
                    badgeFont.setBold(true);
                    painter.setFont(badgeFont);
                    painter.setPen(Qt::white);
                    painter.drawText(badgeRect, Qt::AlignCenter, tag);
                }
            }
        }

        block = block.next();
    }
}
