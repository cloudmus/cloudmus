
#include <QDebug>
#include <QStringList>
#include <QDir>

#include "service_descriptor.h"

ServiceDescriptor::ServiceDescriptor(const QString& filename)
    : description_(filename, QSettings::IniFormat)
{
}

ServiceDescriptor::~ServiceDescriptor()
{}

Service* ServiceDescriptor::create()
{
    service_.reset(new Service(QFileInfo(description_.fileName()).absolutePath() + QDir::separator() +
                               value<QString>("script"), *this));
    return service_.get();
}

Service* ServiceDescriptor::service() const
{
    return service_.get();
}

void ServiceDescriptor::destroy()
{
    service_.reset();
}

QString ServiceDescriptor::pluginPath() const
{
    return QFileInfo(description_.fileName()).absolutePath() + QDir::separator();
}

QString ServiceDescriptor::resource(QString key) const
{
    return pluginPath() + value(key).toString();
}

QString ServiceDescriptor::file(QString file) const
{
    return pluginPath() + file;
}

QIcon ServiceDescriptor::icon(QString def) const
{
    QIcon icon(resource("icon"));
    return !icon.availableSizes().size() ? QIcon::fromTheme(value("icon").toString(), QIcon::fromTheme(def)) : icon;
}

