#ifndef WEBMUSIC_SERVICE_MANAGER_H
#define WEBMUSIC_SERVICE_MANAGER_H

#include <QObject>

#include "service.h"

    
// //
class ServiceManager : public QObject
{
    Q_OBJECT 

public:

    explicit ServiceManager(QObject *parent = 0);
    ~ServiceManager();

    QList<Service_p> list();
    

public Q_SLOTS:
    
private:

};

#endif // WEBMUSIC_PLUGIN_MANAGER_H
