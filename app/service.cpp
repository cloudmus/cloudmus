#include <QFile>
#include <QDebug>
#include <QMap>

#include "tools.h"
#include "service_descriptor.h"
#include "service.h"


Service::Service(const QString& filename, ServiceDescriptor& descriptor)
    : descriptor_(descriptor)
{
    QFile file(filename);
    if (!file.open(QFile::ReadOnly))
        throw std::runtime_error("Can't read script");
    service_ = file.readAll();
}

Service::~Service()
{
}

void Service::initialize(QWebFrame* frame)
{
    frame_ = frame;
    disconnect(frame_, SIGNAL(loadFinished(bool)), this, SLOT(loadFinished(bool)));
    Q_VERIFY(connect(frame_, SIGNAL(loadFinished(bool)), this, SLOT(loadFinished(bool))));
    frame_->load(QUrl(descriptor_.url()));
}

void Service::init_actions()
{
    frame_->evaluateJavaScript("CloudmusService.actions = {};");

    QAction* a;
    a = new QAction(QIcon::fromTheme("media-playback-start"), "play", this);
    install_action(a, "play");

    a = new QAction(QIcon::fromTheme("media-playback-pause"), "pause", this);
    install_action(a, "pause");

    a = new QAction(QIcon::fromTheme("media-playback-stop"), "stop", this);
    install_action(a, "stop");

    a = new QAction(QIcon::fromTheme("media-skip-forward"), "next", this);
    install_action(a, "next");

    a = new QAction(this);
    a->setSeparator(true);
    //     Q_EMIT addAction(a);
}

void Service::install_action(QAction* a, QString action, bool custom)
{
    a->setObjectName(action);
    actions_.push_back(a);
    frame_->addToJavaScriptWindowObject("tmp_action", a);
    if (custom)
        frame_->evaluateJavaScript(QString("CloudmusService.custom_actions.%1 = tmp_action;").arg(action));
    else
        frame_->evaluateJavaScript(QString("CloudmusService.actions.%1 = tmp_action;").arg(action));
    frame_->evaluateJavaScript("delete window.tmp_action;");
    Q_EMIT addAction(a);
}

void Service::loadFinished(bool ok)
{
    if (!ok) return;

    frame_->addToJavaScriptWindowObject("QCloudmusDescriptor", &descriptor_);
    frame_->addToJavaScriptWindowObject("QCloudmusService", this);

    QString init_js = ""
                      "CloudmusService = {QObject: QCloudmusService, descriptor: QCloudmusDescriptor};"
                      "CloudmusService.actions = {}; CloudmusService.custom_actions = {}; "
                      "delete window.QCloudmusService;"
                      "delete window.QCloudmusDescriptor;"
                      ""
                      "CloudmusService.addAction = function(action, text, icon, func){"
                      "  CloudmusService.QObject.addCustomAction(action, text,  icon);"
                      "  CloudmusService.custom_actions[action].triggered.connect(func);"
                      "}";

    frame_->evaluateJavaScript(init_js);
    init_actions();
    frame_->evaluateJavaScript(service_);
}

void Service::addCustomAction(QString action, QString text, QString iconstr)
{
    QIcon icon(descriptor_.file(iconstr));

    QAction* a = new QAction(!icon.availableSizes().size() ? QIcon::fromTheme(iconstr) : icon, text, this);
    install_action(a, action, true);
}


