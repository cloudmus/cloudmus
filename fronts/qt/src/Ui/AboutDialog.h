#pragma once

#include <QDialog>

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
};

} // namespace Ui
