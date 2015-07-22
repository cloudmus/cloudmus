#include "application.h"

#include <QDebug>

#include "options.h"

Application::Application(int& argc, char** argv)
    : QtSingleApplication("cloudmus", argc, argv)
{
    setOrganizationName("gkb");
    setApplicationName("cloudmus");
#ifdef HAVE_QT5
    setApplicationDisplayName(tr("Cloudmus"));
#endif
    setApplicationVersion(tr("0.1.0"));

    options_ = new Options(this);
}

bool Application::notify(QObject* obj, QEvent* event)
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

