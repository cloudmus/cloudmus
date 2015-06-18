#include <QFile>
#include <QDebug>
#include <QtScript/QScriptEngine>

#include "tools.h"
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

void Plugin::initialize(QWebFrame* frame) {
    
    disconnect(frame, SIGNAL(loadFinished(bool)), this, SLOT(loadFinished(bool)));
    Q_VERIFY(connect(frame, SIGNAL(loadFinished(bool)), this, SLOT(loadFinished(bool))));
    frame->load(QUrl(url()));
}



void Plugin::loadFinished(bool ok)
{
    QWebFrame* frame = qobject_cast<QWebFrame*>(sender());

    frame->addToJavaScriptWindowObject("QWebMusicPlugin", this);    
    frame->evaluateJavaScript("WebMusicPlugin = {QObject: QWebMusicPlugin}; delete window.QWebMusicPlugin;");
    frame->evaluateJavaScript(plugin_);

    frame->evaluateJavaScript("\
    (function(){\
        var callFunctionFromQt = function(name, arg1, arg2, arg3) {WebMusicPlugin.actions[name](arg1, arg2, arg3);};\
        WebMusicPlugin.QObject.callSignal.connect(callFunctionFromQt);\
    }());\
    ");
}




