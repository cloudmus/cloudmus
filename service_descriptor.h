#ifndef WEBMUSIC_SERVICE_DESCRIPTOR_H
#define WEBMUSIC_SERVICE_DESCRIPTOR_H

#include <memory>

#include <QSettings>
#include <QVariant>

#include "service.h"

class ServiceDescriptor : public QObject
{
    Q_OBJECT 
public:

    explicit ServiceDescriptor(const QString& filename);
    ~ServiceDescriptor();

    Service* create();
    Service* service() const;
    void destroy();
    
    Q_SCRIPTABLE QString name() const {return value("name").toString();}
    Q_SCRIPTABLE QString url() const {return value("url").toString();}
    Q_SCRIPTABLE QString description() const {return value("description").toString();}
    Q_SCRIPTABLE QIcon icon(QString def = QString()) const;
    
    Q_SCRIPTABLE QVariant value(QString field, QVariant def = QVariant()) const {
        return description_.value(field, def);
    }
    
    template<typename T>
    T value(QString field, const T& def = T()) const {
        return description_.value(field, def).value<T>();
    }
  
private:
    QString pluginPath() const;
    QString resource(QString file) const;
  
private:
    QSettings description_;
    std::unique_ptr<Service> service_;
};

typedef std::shared_ptr<ServiceDescriptor> ServiceDescriptor_p;


#endif // WEBMUSIC_SERVICE_DESCRIPTOR_H
