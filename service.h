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
    
    void play(){Q_EMIT playSignal();};    
    void pause(){Q_EMIT pauseSignal();};
    void stop(){Q_EMIT stopSignal();};
    void next(){Q_EMIT nextSignal();};
    
    void initialize(QWebFrame* frame);
    
    void addCustomAction(QString action, QString text, QString icon) {
        Q_EMIT addActionSignal(action, text, icon);        
    }
    
    void removeAction(QString action) {
        Q_EMIT removeActionSignal(action);        
    }
    
    void callJS(QString function, QVariant arg1 = QVariant(), QVariant arg2 = QVariant(), QVariant arg3 = QVariant()) {
         Q_EMIT callJSAction(function, arg1, arg2, arg3);
    }

private Q_SLOTS:
    void loadFinished(bool ok);
    
  
Q_SIGNALS:
    void addActionSignal(QString action, QString text, QString icon);
    void removeActionSignal(QString action);
    
    void callJSAction(QString action, QVariant arg1, QVariant arg2, QVariant arg3);
    
    void playSignal();
    void pauseSignal();
    void stopSignal();
    void nextSignal();
    
private:
    ServiceDescriptor& descriptor_;
    QByteArray service_;
};

typedef std::shared_ptr<Service> Service_p;

#endif // WEBMUSIC_SERVICE_H
