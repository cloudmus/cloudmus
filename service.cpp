#include <QFile>
#include <QDebug>

#include "tools.h"
#include "service_descriptor.h"
#include "service.h"

Service::Service(const QString& filename, ServiceDescriptor& descriptor)
    : descriptor_(descriptor)
{
    QScriptEngine engine;

    QFile file(filename);
    if (!file.open(QFile::ReadOnly)) {
        throw std::runtime_error("can;t read script");
    }
    service_ = file.readAll();
};

Service::~Service()
{
};

void Service::initialize(QWebFrame* frame)
{
    disconnect(frame, SIGNAL(loadFinished(bool)), this, SLOT(loadFinished(bool)));
    Q_VERIFY(connect(frame, SIGNAL(loadFinished(bool)), this, SLOT(loadFinished(bool))));
    frame->load(QUrl(descriptor_.url()));
}

void Service::loadFinished(bool ok)
{
    QWebFrame* frame = qobject_cast<QWebFrame*>(sender());

    frame->addToJavaScriptWindowObject("QWebMusicDescriptor", &descriptor_);
    frame->addToJavaScriptWindowObject("QWebMusicService", this);
    frame->evaluateJavaScript("WebMusicService = {QObject: QWebMusicService, descriptor: QWebMusicDescriptor}; delete window.QWebMusicService; delete window.QWebMusicDescriptor");
    frame->evaluateJavaScript(service_);

    frame->evaluateJavaScript("\
    (function(){\
        var callFunctionFromQt = function(name, arg1, arg2, arg3) {WebMusicService.actions[name](arg1, arg2, arg3);};\
        WebMusicService.QObject.callSignal.connect(callFunctionFromQt);\
    }());\
    ");
}

