#ifndef WEBMUSIC_PLUGIN_H
#define WEBMUSIC_PLUGIN_H

#include <QObject>
#include <QVariant>
#include <QWebFrame>

class Plugin : public QObject
{
    Q_OBJECT 
    Q_PROPERTY(QString name READ name WRITE setName)
    Q_PROPERTY(QString url READ url WRITE setUrl)
    Q_PROPERTY(QVariant actions READ actions WRITE setActions)

public:

    explicit Plugin(const QString& filename, QObject *parent = 0);
    ~Plugin();

Q_SCRIPTABLE QString name() const;
Q_SCRIPTABLE void setName(QString name);

Q_SCRIPTABLE QString url() const;
Q_SCRIPTABLE void setUrl(QString url);

Q_SCRIPTABLE QVariant actions() const {return actions_;};
Q_SCRIPTABLE void setActions(QVariant actions) {actions_ = actions;};

public Q_SLOTS:
    void hello(QWebFrame* frame);

private:
    QString name_;
    QString url_;
    QVariant actions_;
};

#endif // WEBMUSIC_PLUGIN_H
