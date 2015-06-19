
#include <QDebug>
#include <QFile>
#include <QMenu>
#include <QSignalMapper>
#include <QWebFrame>
#include <QWebInspector>
#include <QWebSettings>
#include <QWebView>

#include "service_manager.h"
#include "tools.h"

#include "ui_mainwindow.h"
#include "mainwindow.h"

QWebView* webEngine;

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent), 
    ui_(new Ui::MainWindow),
    tray_(this)
{
    ui_->setupUi(this);
    
    QWebSettings::globalSettings()->setAttribute(QWebSettings::PluginsEnabled, true);
    QWebSettings::globalSettings()->setAttribute(QWebSettings::JavascriptEnabled, true);
    QWebSettings::globalSettings()->setAttribute(QWebSettings::JavaEnabled, true);
    QWebSettings::globalSettings()->setAttribute(QWebSettings::LocalStorageEnabled, true);
    QWebSettings::globalSettings()->setAttribute(QWebSettings::OfflineStorageDatabaseEnabled,true);
    QWebSettings::globalSettings()->setAttribute(QWebSettings::OfflineWebApplicationCacheEnabled,true);
    QWebSettings::globalSettings()->setAttribute(QWebSettings::DeveloperExtrasEnabled, true);
    QWebSettings::globalSettings()->setAttribute(QWebSettings::XSSAuditingEnabled, true);
    QWebSettings::enablePersistentStorage("/tmp");

    QWebSettings::globalSettings()->setOfflineStorageDefaultQuota(5*1024*1024);
    QWebSettings::globalSettings()->setOfflineWebApplicationCacheQuota(5*1024*1024);


    webEngine = new QWebView(centralWidget());
    centralWidget()->layout()->addWidget(webEngine);
    
    tray_.setIcon(QIcon("icons:cloudmus.png"));
    tray_.setVisible(true);
    tray_.setContextMenu(new QMenu());
    title_action_ = tray_.contextMenu()->addAction("");
    title_action_->setEnabled(false);
    title_action_->setVisible(false);
    services_menu_ = tray_.contextMenu()->addMenu("services");
    
    ServiceManager manager;
    services_ = manager.list();
    
    for (auto p : services_) {
        addService(p);
    }
    
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

void MainWindow::addService(Service_p service)
{
    QAction* a = services_menu_->addAction(QIcon::fromTheme("123", QIcon::fromTheme(fallbackIcon("preferences-plugin"))), service->name());
    a->setCheckable(true);
    Q_VERIFY(::connect(a, SIGNAL(triggered(bool)), [this, service, a](){
        if (a->isChecked()) {
            activateService(service);
        }
        else {
            activateService(Service_p());
        }
    }));
}

void MainWindow::activateService(Service_p service)
{
    title_action_->setText("");
    title_action_->setVisible(false);
    if (s_.get()) {
        s_->finalize(webEngine->page()->mainFrame());
        disconnect(s_.get(), SIGNAL(addActionSignal(QString, QString, QString)), this, SLOT(addAction(QString, QString, QString)));
        Q_EMIT finalized(service);
    }
    
    s_ = service;
    
    if (s_.get()) {
        title_action_->setText(s_->name());
        title_action_->setVisible(true);
        QFont f = title_action_->font();
        f.setBold(true);
        title_action_->setFont(f);
        
        Q_VERIFY(connect(s_.get(), SIGNAL(addActionSignal(QString, QString, QString)), this, SLOT(addAction(QString, QString, QString))));
        s_->initialize(webEngine->page()->mainFrame());
    }
}

void MainWindow::addAction(QString text, QString icon, QString action)
{
    QMenu* menu = tray_.contextMenu();
    QSignalMapper* mapper = new QSignalMapper(s_.get());
    QAction* a = menu->addAction(QIcon::fromTheme(icon, QIcon::fromTheme(fallbackIcon(action))), text);
    Q_VERIFY(connect(a, SIGNAL(triggered()), mapper, SLOT(map())));
    mapper->setMapping(a, action);
    Q_VERIFY(connect(mapper, SIGNAL(mapped(const QString &)), s_.get(), SLOT(call(QString))));
    Q_VERIFY(connect(this, SIGNAL(finalized(Service_p)), a, SLOT(deleteLater())));
}

