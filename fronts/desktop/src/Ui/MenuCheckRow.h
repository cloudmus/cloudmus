#pragma once

#include <QWidget>

class QCheckBox;

namespace Ui {

// A check box row for a QMenu (via QWidgetAction) that behaves like a
// menu item: highlighted on hover with the same tinted fill
// Theme::CloudMusStyle paints for items, toggled by a click anywhere on
// the row — and, unlike a checkable QAction, toggling it doesn't close
// the menu, so several can be changed in one go.
class MenuCheckRow : public QWidget {
    Q_OBJECT

public:
    MenuCheckRow(const QString& text, QWidget* parent);

    QCheckBox* checkBox() const { return box_; }

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    QCheckBox* box_;
};

} // namespace Ui
