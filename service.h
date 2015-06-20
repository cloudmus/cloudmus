#ifndef WEBMUSIC_SERVICE_H
#define WEBMUSIC_SERVICE_H

#include <memory>

#include <QObject>
#include <QAction>
#include <QVariant>
#include <QWebFrame>

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
    
    void addCustomAction(QString action, QString text, QString icon);
    
private Q_SLOTS:
    void loadFinished(bool ok);
  
Q_SIGNALS:
    void addAction(QAction* action);
    
private:
    void init_actions();
    void install_action(QAction* a, QString action, bool custom = false);
    
private:
    ServiceDescriptor& descriptor_;
    QByteArray service_;
    QWebFrame* frame_;
    QList<QAction*> actions_;
};

typedef std::shared_ptr<Service> Service_p;

#endif // WEBMUSIC_SERVICE_H
