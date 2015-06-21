#pragma once

#include <QObject>

#include "service.h"
#include "service_descriptor.h"


// //
class ServiceManager : public QObject
{
    Q_OBJECT

public:

    explicit ServiceManager(QObject* parent = 0);
    ~ServiceManager();

    QList<ServiceDescriptorPtr> list();


public Q_SLOTS:

private:

};
