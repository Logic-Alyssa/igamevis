#pragma once

#include <IQComponents/Dialog/igQtChromeFramelessDialog.h>
#include <IQCore/igQtExportModule.h>

class QHBoxLayout;
class QPushButton;

class IG_QT_MODULE_EXPORT igQtDarkFramelessMessageDialog : public igQtChromeFramelessDialog {
    Q_OBJECT
public:
    explicit igQtDarkFramelessMessageDialog(QWidget* parent, const QString& title, const QString& text,
                                            bool useInformationIcon = false);

    // 在「确定」按钮左侧追加一个按钮（可选），返回按钮指针供调用方连接信号
    QPushButton* addButton(const QString& text);

private:
    QHBoxLayout* m_buttonRow{nullptr};
};

IG_QT_MODULE_EXPORT void igQtShowDarkFramelessMessage(QWidget* parent, const QString& title, const QString& text,
                                                     bool useInformationIcon = false);
