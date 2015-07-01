#include "aboutdialog.h"
#include "ui_aboutdialog.h"

AboutDialog::AboutDialog(QWidget* parent) :
    QDialog(parent),
    ui(new Ui::AboutDialog)
{
    ui->setupUi(this);

    QPalette transpPalette;
    transpPalette.setColor(QPalette::Base, Qt::transparent);
    transpPalette.setColor(QPalette::Text, transpPalette.color(QPalette::WindowText));
    ui->aboutBrowser->setPalette(transpPalette);
    ui->whatsNewBrowser->setPalette(transpPalette);
    ui->authorsBrowser->setPalette(transpPalette);

}

AboutDialog::~AboutDialog()
{
    delete ui;
}
