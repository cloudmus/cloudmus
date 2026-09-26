#pragma once

#include <QDialog>

class QLabel;

namespace Ui {

// A short themed notice — a heading, a paragraph, and buttons — in place of
// QMessageBox::information(), which the app's QSS can't reach (see
// AboutDialog). An optional action button beside Close; exec() returns
// QDialog::Accepted when it was clicked.
class InfoDialog : public QDialog {
    Q_OBJECT

public:
    InfoDialog(const QString& title, const QString& heading, const QString& text, const QString& actionText,
        QWidget* parent = nullptr);

    QSize sizeHint() const override;
};

} // namespace Ui
