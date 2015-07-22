#pragma once

#include "3rdparty/qtsingleapplication/qtsingleapplication.h"

#include "pointers.h"

class Application : public QtSingleApplication
{
public:
    Application(int& argc, char** argv);

    bool notify(QObject* obj, QEvent* event) override;

    Options* options() const { return options_; }

private:
    Options* options_;
};
