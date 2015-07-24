#pragma once

#include "optionspage.h"

namespace Ui {
class ShortcutsOptionsPage;
}

class ShortcutsOptionsPage : public OptionsPage
{
    Q_OBJECT

public:
    explicit ShortcutsOptionsPage(QWidget *parent = 0);
    ~ShortcutsOptionsPage();

private:
    Ui::ShortcutsOptionsPage *ui;
};
