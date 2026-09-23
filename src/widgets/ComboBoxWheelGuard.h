#ifndef COMBOBOXWHEELGUARD_H
#define COMBOBOXWHEELGUARD_H

#include <QObject>

/*****************************************************
类名称：ComboBoxWheelGuard
类功能：全局事件过滤器，屏蔽全部下拉选择框上的鼠标滚轮选值行为
*****************************************************/
class ComboBoxWheelGuard final : public QObject
{
    Q_OBJECT

public:
    // 创建下拉选择框滚轮过滤器。
    explicit ComboBoxWheelGuard(QObject* parent = nullptr);
    // 释放下拉选择框滚轮过滤器。
    ~ComboBoxWheelGuard() override;

protected:
    // 拦截下拉选择框及其编辑框的滚轮事件。
    bool eventFilter(QObject* watched, QEvent* event) override;
};

#endif // COMBOBOXWHEELGUARD_H
