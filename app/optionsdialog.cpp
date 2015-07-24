#include "optionsdialog.h"
#include "ui_optionsdialog.h"

#include "applicationoptionspage.h"
#include "shortcutsoptionspage.h"

Q_DECLARE_METATYPE(OptionsPage*)

OptionsDialog::OptionsDialog(QWidget* parent) :
    QDialog(parent),
    ui(new Ui::OptionsDialog)
{
    ui->setupUi(this);

    addPage(new ApplicationOptionsPage(this));
    addPage(new ShortcutsOptionsPage(this));

    connect(ui->listWidget, SIGNAL(currentItemChanged(QListWidgetItem*, QListWidgetItem*)), SLOT(updateCurrentPage()));

    ui->listWidget->setCurrentRow(0);

    updateCurrentPage();
}

OptionsDialog::~OptionsDialog()
{
    delete ui;
}

void OptionsDialog::updateCurrentPage()
{
    QListWidgetItem* item = ui->listWidget->currentItem();
    if (!item) {
        ui->stackedWidget->setCurrentWidget(0);
        return;
    }
    OptionsPage* page = item->data(Qt::UserRole).value<OptionsPage*>();
    ui->stackedWidget->setCurrentWidget(page);
}

void OptionsDialog::addPage(OptionsPage* page)
{
    ui->stackedWidget->addWidget(page);

    auto item = new QListWidgetItem;
    item->setText(page->windowTitle());
    item->setData(Qt::UserRole, QVariant::fromValue(page));
    ui->listWidget->addItem(item);
}
