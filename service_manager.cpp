
#include <QCoreApplication>
#include <QDebug>
#include <QDesktopServices>
#include <QDir>

#include "service_manager.h"

ServiceManager::ServiceManager(QObject* parent)
    : QObject(parent)
{
    QDir dir;
    dir.mkpath(QDesktopServices::storageLocation(QDesktopServices::DataLocation) + QDir::separator() + QString("services"));
}


ServiceManager::~ServiceManager()
{

}

QList<ServiceDescriptor_p> ServiceManager::list()
{
    static const QStringList paths = {
        QString("/usr/share/%1/%2/services").arg(qApp->organizationName()).arg(qApp->applicationName()),
        QDesktopServices::storageLocation(QDesktopServices::DataLocation) + QDir::separator() + QString("services"),
        QDir::currentPath() + QDir::separator() + QString("services"),
    };

    QList<ServiceDescriptor_p> result;
    
    for (auto path : paths) {
        QDir dir(path);
        for (auto info : dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            result << ServiceDescriptor_p(new ServiceDescriptor(info.absoluteFilePath() + QDir::separator() + "/description.ini"));
        }
    }

    return result;
}
