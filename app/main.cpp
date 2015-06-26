
#include <stdexcept>

#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QTextCodec>

#include "mainwindow.h"

class Application: public QApplication
{
public:
    Application(int& argc, char** argv)
        : QApplication(argc, argv)
    {}

    virtual bool notify(QObject* obj, QEvent* event)
    {
        try {
            return QApplication::notify(obj, event);
        } catch (std::exception& e) {
            qDebug() << "Unhandled exception in notify: " << e.what();
            qDebug() << "In object:" << obj->staticMetaObject.className() << obj->objectName() << "Event:" << event->type();
            Q_ASSERT(false);
        }
        return false;
    }
};

int main(int argc, char* argv[])
{
    Application app(argc, argv);
    app.setOrganizationName("gkb");
    app.setApplicationName("cloudmus");

#ifdef HAVE_QT5
#else
    QTextCodec::setCodecForTr(QTextCodec::codecForName("utf8"));
#endif

    QDir::addSearchPath("icons", QString(":icons/images/"));
    QDir::addSearchPath("images", QString(":icons/images/"));

    MainWindow w;
    w.show();

    w.resize(1000, 700);

    return app.exec();
}