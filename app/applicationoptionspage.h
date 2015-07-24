#pragma once

#include "optionspage.h"

class Ui_ApplicationOptionsPage;

class ApplicationOptionsPage : public OptionsPage
{
    Q_OBJECT
public:
    ApplicationOptionsPage(QWidget* parent = 0);
    ~ApplicationOptionsPage();

private slots:
    void changedByUser();
    void updateOptions();

private:
    Ui_ApplicationOptionsPage* ui;
    bool blockUpdate_;
    bool blockChange_;
};
