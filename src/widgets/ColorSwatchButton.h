#ifndef COLORSWATCHBUTTON_H
#define COLORSWATCHBUTTON_H

#include <QAbstractButton>
#include <QColor>

class QEvent;
class QPaintEvent;

class ColorSwatchButton final : public QAbstractButton
{
    Q_OBJECT

public:
    // 创建用于选择标记颜色的色块按钮。
    explicit ColorSwatchButton(QWidget* parent = nullptr);
    // 释放色块按钮。
    ~ColorSwatchButton() override;
    // 获取当前颜色。
    QColor color() const;
    // 设置当前颜色并刷新显示。
    void setColor(const QColor& color);

protected:
    // 处理鼠标悬停以刷新边框。
    bool event(QEvent* event) override;
    // 绘制圆角色块和边框。
    void paintEvent(QPaintEvent* event) override;

private:
    QColor _color;
};

#endif // COLORSWATCHBUTTON_H
