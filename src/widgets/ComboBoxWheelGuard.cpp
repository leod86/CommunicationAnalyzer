#include "widgets/ComboBoxWheelGuard.h"

#include <QComboBox>
#include <QEvent>
#include <QLineEdit>

/*****************************************************
函数名称：ComboBoxWheelGuard::ComboBoxWheelGuard(QObject* parent)
入口参数：parent为父对象
出口参数：无
函数功能：初始化下拉选择框滚轮过滤器
*****************************************************/
ComboBoxWheelGuard::ComboBoxWheelGuard(QObject* parent)
    : QObject(parent)
{
}

/*****************************************************
函数名称：ComboBoxWheelGuard::~ComboBoxWheelGuard()
入口参数：无
出口参数：无
函数功能：释放下拉选择框滚轮过滤器
*****************************************************/
ComboBoxWheelGuard::~ComboBoxWheelGuard() = default;

/*****************************************************
函数名称：bool ComboBoxWheelGuard::eventFilter(QObject* watched, QEvent* event)
入口参数：watched为事件接收对象，event为事件对象
出口参数：true表示事件不再交给watched处理
函数功能：忽略下拉选择框及其可编辑输入框上的滚轮事件，
          使选中项不随滚轮变化，其余控件行为不受影响
*****************************************************/
bool ComboBoxWheelGuard::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() != QEvent::Wheel)
    {
        return QObject::eventFilter(watched, event);
    }

    // 可编辑下拉框的输入框会把滚轮事件冒泡给下拉框，需要一并拦截。
    const auto* lineEdit = qobject_cast<QLineEdit*>(watched);
    const bool isComboBoxEditor = lineEdit != nullptr
        && qobject_cast<QComboBox*>(lineEdit->parentWidget()) != nullptr;
    if (qobject_cast<QComboBox*>(watched) != nullptr || isComboBoxEditor)
    {
        event->ignore();
        return true;
    }

    return QObject::eventFilter(watched, event);
}
