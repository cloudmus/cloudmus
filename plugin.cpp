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
    plugin_ = file.readAll();

    QScriptValue self = engine.newQObject(this);
    engine.globalObject().setProperty("WebMusicPlugin", self);
    engine.evaluate(plugin_);
    actions_ = QVariant();
};

Plugin::~Plugin() {

};

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

QByteArray Plugin::plugin() const
{
    return plugin_;
}

void Plugin::initialize(QWebFrame* frame) {
    frame->evaluateJavaScript("WebMusicPlugin = {};");
    frame->evaluateJavaScript(plugin_);
    
    frame->addToJavaScriptWindowObject("QWebMusicPlugin", this);
    frame->evaluateJavaScript("WebMusicPlugin.QObject = QWebMusicPlugin; delete window.QWebMusicPlugin;");
     
    frame->evaluateJavaScript("\
    (function(){\
        var callFunctionFromQt = function(name, arg1, arg2, arg3) {WebMusicPlugin.actions[name](arg1, arg2, arg3);};\
        WebMusicPlugin.QObject.call_js.connect(callFunctionFromQt);\
    }());\
    ");
}


void Plugin::call(QString function, QVariant arg1, QVariant arg2, QVariant arg3)
{
    Q_EMIT call_js(function, arg1, arg2, arg3);
}

