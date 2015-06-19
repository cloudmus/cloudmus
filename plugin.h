#ifndef WEBMUSIC_PLUGIN_H
#define WEBMUSIC_PLUGIN_H

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

    
// //
class Plugin : public QObject
{
    Q_OBJECT 
    Q_PROPERTY(QString name READ name WRITE set_name)
    Q_PROPERTY(QString url  READ url  WRITE set_url)
    Q_PROPERTY(QVariant actions READ actions WRITE set_actions)

public:

    explicit Plugin(const QString& filename, QObject *parent = 0);
    ~Plugin();

    ADD_PARAM(name, QString, QString());
    ADD_PARAM(url,  QString, QString());
    ADD_PARAM(actions, QVariant, QVariant());
    
Q_SIGNALS:
    void name_changed(QString);
    void url_changed(QString);
    void actions_changed(QVariant);
    
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
    QByteArray plugin_;
    QString name_;
    QString url_;
    QVariant actions_;
};

typedef std::shared_ptr<Plugin> Plugin_p;

#endif // WEBMUSIC_PLUGIN_H
