#pragma once

#include <QDialog>

namespace Ui {
class OptionsDialog;
}

class OptionsPage;

class OptionsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit OptionsDialog(QWidget *parent = 0);
    ~OptionsDialog();

private slots:
    void updateCurrentPage();

private:
    void addPage(OptionsPage* page);

    Ui::OptionsDialog *ui;
};
