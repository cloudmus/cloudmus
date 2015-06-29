#include <QDebug>
#include <QDir>
#include <QTextCodec>

#include "mainwindow.h"
#include "application.h"

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
    w.show();
    w.resize(1000, 700);

    return app.exec();
}
