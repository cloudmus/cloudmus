#pragma once

#include "3rdparty/qtsingleapplication/qtsingleapplication.h"

class Application : public QtSingleApplication
{
public:
    Application(int& argc, char** argv);

    bool notify(QObject* obj, QEvent* event) override;
};
