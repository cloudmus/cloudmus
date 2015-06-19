#ifndef WEBMUSIC_SERVICE_H
#define WEBMUSIC_SERVICE_H

#include <memory>

#include <QObject>
#include <QVariant>
#include <QWebFrame>

#define ADD_PARAM(name, type, def) \
    type name() const {\
        return name##_;\
    }\
    void set_##name(const type& value) {\
        name##_ = value;\
        Q_EMIT name##_changed(value);\
    }\
    void reset_##name() {\
        set_##name(default_##name());\
    } \
    type default_##name() const {\
        return def;\
    }

    
class ServiceDescriptor;
    
// //
class Service : public QObject
{
    Q_OBJECT 

public:

    explicit Service(const QString& filename, ServiceDescriptor& descriptor);
    ~Service();

    ServiceDescriptor& descriptor() {return descriptor_;};
    

public Q_SLOTS:
    void initialize(QWebFrame* frame);
    
    void addAction(QString text, QString icon, QString action) {
        Q_EMIT addActionSignal(text, icon, action);        
    }
    
    void call(QString function, QVariant arg1 = QVariant(), QVariant arg2 = QVariant(), QVariant arg3 = QVariant()) {
         Q_EMIT callSignal(function, arg1, arg2, arg3);
    }

private Q_SLOTS:
    void loadFinished(bool ok);
    
  
Q_SIGNALS:
    void callSignal(QString function, QVariant arg1, QVariant arg2, QVariant arg3);
    void addActionSignal(QString text, QString icon, QString callback);
    
private:
    ServiceDescriptor& descriptor_;
    QByteArray service_;
};

typedef std::shared_ptr<Service> Service_p;

#endif // WEBMUSIC_SERVICE_H
