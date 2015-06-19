
#include <QDebug>
#include <QFile>
#include <QMenu>
#include <QSignalMapper>
#include <QWebFrame>
#include <QWebInspector>
#include <QWebSettings>
#include <QWebView>

#include "plugin_manager.h"
#include "tools.h"

#include "ui_mainwindow.h"
#include "mainwindow.h"



MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent), 
    ui_(new Ui::MainWindow),
    tray_(this)
{
    ui_->setupUi(this);
    
    QWebSettings::globalSettings()->setAttribute(QWebSettings::PluginsEnabled, true);
    QWebSettings::globalSettings()->setAttribute(QWebSettings::JavascriptEnabled, true);
    QWebSettings::globalSettings()->setAttribute(QWebSettings::QWebSettings::JavaEnabled, true);
    QWebSettings::globalSettings()->setAttribute(QWebSettings::LocalStorageEnabled, true);
    QWebSettings::globalSettings()->setAttribute(QWebSettings::OfflineStorageDatabaseEnabled,true);
    QWebSettings::globalSettings()->setAttribute(QWebSettings::OfflineWebApplicationCacheEnabled,true);
    QWebSettings::globalSettings()->setAttribute(QWebSettings::DeveloperExtrasEnabled, true);
    QWebSettings::globalSettings()->setAttribute(QWebSettings::XSSAuditingEnabled, true);
    QWebSettings::enablePersistentStorage("/tmp");

    QWebSettings::globalSettings()->setOfflineStorageDefaultQuota(5*1024*1024);
    QWebSettings::globalSettings()->setOfflineWebApplicationCacheQuota(5*1024*1024);


    QWebView* webEngine = new QWebView(centralWidget());
    centralWidget()->layout()->addWidget(webEngine);
    
    tray_.setIcon(QIcon("icons:cloudmus.png"));
    tray_.setVisible(true);
    tray_.setContextMenu(new QMenu());
    
    PluginManager manager;
    p_ = manager.list().front();
    
    Q_VERIFY(connect(p_.get(), SIGNAL(addActionSignal(QString, QString, QString)), this, SLOT(addAction(QString, QString, QString))));
    p_->initialize(webEngine->page()->mainFrame());
}

MainWindow::~MainWindow()
{}

QString fallbackIcon(QString icon) {
    const static std::map<QString, QString> fallbackIcons = {
        {"play",  "media-playback-start"},
        {"stop",  "media-playback-stop"},
        {"pause", "media-playback-pause"},
        {"next",  "media-skip-forward"},
    };

    const auto it = fallbackIcons.find(icon);
    return (it == fallbackIcons.end()) ? icon : it->second;
}

void MainWindow::addAction(QString text, QString icon, QString action)
{

    
    
    QMenu* menu = tray_.contextMenu();
    QSignalMapper* mapper = new QSignalMapper(p_.get());
    QAction* a = menu->addAction(QIcon::fromTheme(icon, QIcon::fromTheme(fallbackIcon(action))), text);
    Q_VERIFY(connect(a, SIGNAL(triggered()), mapper, SLOT(map())));
    mapper->setMapping(a, action);
    Q_VERIFY(connect(mapper, SIGNAL(mapped(const QString &)), p_.get(), SLOT(call(QString))));
}

