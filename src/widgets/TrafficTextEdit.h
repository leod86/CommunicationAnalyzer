#ifndef TRAFFICTEXTEDIT_H
#define TRAFFICTEXTEDIT_H

#include "ElaPlainTextEdit.h"

class QPaintEvent;
class QWheelEvent;

class TrafficTextEdit final : public ElaPlainTextEdit
{
    Q_OBJECT

public:
    // 创建支持RX/TX圆角标签的通讯文本控件。
    explicit TrafficTextEdit(QWidget* parent = nullptr);
    // 释放通讯文本控件。
    ~TrafficTextEdit() override;
    // 设置接收窗口文字字号。
    void setTextPointSize(int pointSize);
    // 获取接收窗口文字字号。
    int textPointSize() const;

signals:
    // 通知页面保存用户调整后的字号。
    void textPointSizeChanged(int pointSize);

protected:
    // 绘制文本内容并覆盖RX/TX圆角标签。
    void paintEvent(QPaintEvent* event) override;
    // 处理Ctrl加鼠标滚轮的字号缩放。
    void wheelEvent(QWheelEvent* event) override;

private:
    // 在可见文本块的RX/TX文字位置绘制圆角徽标。
    void drawDirectionBadges();
};

#endif // TRAFFICTEXTEDIT_H
