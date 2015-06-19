
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
    service_.reset(new Service(QFileInfo(description_.fileName()).absolutePath() + QDir::separator() + value<QString>("script"), *this));
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

