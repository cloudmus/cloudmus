#include "shortcutsoptionspage.h"
#include "ui_shortcutsoptionspage.h"

ShortcutsOptionsPage::ShortcutsOptionsPage(QWidget *parent)
    : OptionsPage(parent)
    , ui(new Ui::ShortcutsOptionsPage)
{
    ui->setupUi(this);
}

ShortcutsOptionsPage::~ShortcutsOptionsPage()
{
    delete ui;
}
