#include <QDebug>
#include <QDir>
#include <QTextCodec>

#include "mainwindow.h"
#include "application.h"
#include "options.h"

int main(int argc, char* argv[])
{
    Application app(argc, argv);
    if (app.isRunning()) {
        app.sendMessage("show");
        qDebug() << "Previous instance activated";
        return 0;
    }

#ifdef HAVE_QT5
#else
    QTextCodec::setCodecForTr(QTextCodec::codecForName("utf8"));
#endif

    QDir::addSearchPath("icons", QString(":icons/images/"));
    QDir::addSearchPath("images", QString(":icons/images/"));

    MainWindow w;
    app.setActivationWindow(&w);
    w.resize(1000, 700);

    if (!Options::value<Options::StartHiddenOption>())
        w.show();

    return app.exec();
}
