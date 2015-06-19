#ifndef WEBMUSIC_PLUGIN_MANAGER_H
#define WEBMUSIC_PLUGIN_MANAGER_H

#include <QObject>

#include "plugin.h"

    
// //
class PluginManager : public QObject
{
    Q_OBJECT 

public:

    explicit PluginManager(QObject *parent = 0);
    ~PluginManager();

    QList<Plugin_p> list();
    

public Q_SLOTS:
    
private:

};

#endif // WEBMUSIC_PLUGIN_MANAGER_H
