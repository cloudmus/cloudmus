
#include <QCoreApplication>
#include <QDebug>

#ifdef HAVE_QT5
#include <QStandardPaths>
#else
#include <QDesktopServices>
#endif

#include <QDir>

#include "service_manager.h"

ServiceManager::ServiceManager(QObject* parent)
    : QObject(parent)
{

#ifdef HAVE_QT5
    QString local = QStandardPaths::writableLocation(QStandardPaths::DataLocation);
#else
    QString local = QDesktopServices::storageLocation(QDesktopServices::DataLocation);
#endif

    QDir dir;
    dir.mkpath(local + QDir::separator() + QString("services"));

}


ServiceManager::~ServiceManager()
{

}

QList<ServiceDescriptorPtr> ServiceManager::list()
{
#ifdef HAVE_QT5
    QStringList paths = {
        QString("/usr/share/%1/%2").arg(qApp->organizationName()).arg(qApp->applicationName()),
        QStandardPaths::writableLocation(QStandardPaths::DataLocation),
        QDir::currentPath(),
    };
#else
    QStringList paths = {
        QString("/usr/share/%1/%2").arg(qApp->organizationName()).arg(qApp->applicationName()),
        QDesktopServices::storageLocation(QDesktopServices::DataLocation),
        QDir::currentPath(),
    };
#endif

    QList<ServiceDescriptorPtr> result;

    qDebug() << paths;

    for (auto path : paths) {
        QDir dir(path + QDir::separator() + QString("services"));
        for (auto info : dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
            result << std::make_shared<ServiceDescriptor>(info.absoluteFilePath() + QDir::separator() + "/description.ini");
    }

    return result;
}
