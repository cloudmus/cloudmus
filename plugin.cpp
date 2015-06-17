#include <QFile>
#include <QDebug>
#include <QtScript/QScriptEngine>


#include "plugin.h"

Plugin::Plugin(const QString& filename, QObject *parent)
    : QObject(parent)
{
    QScriptEngine engine;

    QFile file(filename);
    file.open(QFile::ReadOnly);

    QScriptValue self = engine.newQObject(this);
    engine.globalObject().setProperty("WebMusicPlugin", self);
    engine.evaluate(file.readAll());
}

Plugin::~Plugin() {

}

QString Plugin::name() const {
    return name_;
};

void Plugin::setName(QString name) {
    name_ = name;
};

QString Plugin::url() const {
    return url_;
};

void Plugin::setUrl(QString url) {
    url_ = url;
};

void Plugin::hello(QWebFrame* frame) {
  qDebug() << "hello";
    QVariant v0 = frame->evaluateJavaScript("console.log(111);");
    QVariant v1 = frame->evaluateJavaScript("console.log(WebMusicPlugin.actions);");
    QVariant v2 = frame->evaluateJavaScript("console.log(222);");
  qDebug() << v0;
    QVariant v = frame->evaluateJavaScript ("WebMusicPlugin.actions.hello()i;");
    qDebug() << v;
};
