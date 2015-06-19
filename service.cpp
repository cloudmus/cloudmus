#include <QFile>
#include <QDebug>
#include <QtScript/QScriptEngine>

#include "tools.h"
#include "service.h"

Service::Service(const QString& filename, QObject *parent)
    : QObject(parent)
{
    QScriptEngine engine;

    QFile file(filename);
    file.open(QFile::ReadOnly);
    service_ = file.readAll();

    QScriptValue self = engine.newQObject(this);
    engine.globalObject().setProperty("WebMusicService", self);
    engine.evaluate(service_);
};

Service::~Service()
{
};

void Service::initialize(QWebFrame* frame)
{
    disconnect(frame, SIGNAL(loadFinished(bool)), this, SLOT(loadFinished(bool)));
    Q_VERIFY(connect(frame, SIGNAL(loadFinished(bool)), this, SLOT(loadFinished(bool))));
    frame->load(QUrl(url()));
}

void Service::finalize(QWebFrame* frame)
{
    disconnect(frame, SIGNAL(loadFinished(bool)), this, SLOT(loadFinished(bool)));
    disconnect(this);
}

void Service::loadFinished(bool ok)
{
    QWebFrame* frame = qobject_cast<QWebFrame*>(sender());

    frame->addToJavaScriptWindowObject("QWebMusicService", this);    
    frame->evaluateJavaScript("WebMusicService = {QObject: QWebMusicService}; delete window.QWebMusicService;");
    frame->evaluateJavaScript(service_);

    frame->evaluateJavaScript("\
    (function(){\
        var callFunctionFromQt = function(name, arg1, arg2, arg3) {WebMusicService.actions[name](arg1, arg2, arg3);};\
        WebMusicService.QObject.callSignal.connect(callFunctionFromQt);\
    }());\
    ");
}

