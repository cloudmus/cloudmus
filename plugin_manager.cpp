
#include <QDir>
#include <QDebug>

#include "plugin_manager.h"

PluginManager::PluginManager(QObject* parent)
    : QObject(parent)
{

}


PluginManager::~PluginManager()
{

}

QList<Plugin_p> PluginManager::list()
{
    QDir dir("services/");
    
    QList<Plugin_p> result;
    
    for (auto info : dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        qDebug() << info.absoluteFilePath();
        result << Plugin_p(new Plugin(info.absoluteFilePath() + "/script.js"));
    }

   
    return result;
}
