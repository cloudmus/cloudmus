#ifndef WEBMUSIC_SERVICE_MANAGER_H
#define WEBMUSIC_SERVICE_MANAGER_H

#include <QObject>

#include "service.h"
#include "service_descriptor.h"

    
// //
class ServiceManager : public QObject
{
    Q_OBJECT 

public:

    explicit ServiceManager(QObject *parent = 0);
    ~ServiceManager();

    QList<ServiceDescriptor_p> list();
    

public Q_SLOTS:
    
private:

};

#endif // WEBMUSIC_PLUGIN_MANAGER_H
