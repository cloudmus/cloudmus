#pragma once

#include <QDialog>

class QLabel;

namespace Ui {

// A short themed notice — a heading, a paragraph, and buttons — in place of
// QMessageBox::information(), which the app's QSS can't reach (see
// AboutDialog). An optional action button beside Close (`closeText`, if
// given, renames it — e.g. to Cancel for a yes/no question); exec()
// returns QDialog::Accepted when the action was clicked.
class InfoDialog : public QDialog {
    Q_OBJECT

public:
    InfoDialog(const QString& title, const QString& heading, const QString& text, const QString& actionText,
        QWidget* parent = nullptr, const QString& closeText = QString());

    QSize sizeHint() const override;
};

} // namespace Ui
