#pragma once

#include <QDialog>

class QLabel;

namespace Ui {

// Replaces the native QMessageBox::about(...) this used to be — a
// QMessageBox is a stock Qt dialog with no styling hook the app's QSS can
// reach (same reason ThemedToolTip replaces QToolTip and
// Theme::CloudMusStyle hand-paints QMenu: some native Qt popups just
// aren't stylable in place).
class AboutDialog : public QDialog {
    Q_OBJECT

public:
    explicit AboutDialog(QWidget* parent = nullptr);

    QSize sizeHint() const override;

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    // Keeps the minimum height layout-driven but correct at the CURRENT
    // width: Qt's minimum for a top-level window ignores height-for-width,
    // counting the word-wrapped description as one line, so the window
    // could be squeezed until margins and the header collapsed.
    void updateMinimumHeight();

    QLabel* description_ = nullptr;
};

} // namespace Ui
