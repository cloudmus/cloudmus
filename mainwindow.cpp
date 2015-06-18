#include "mainwindow.h"
#include "ui_mainwindow.h"


#include <QFile>
#include <QMenu>
#include <QSignalMapper>
#include <QWebFrame>
#include <QWebInspector>
#include <QWebSettings>
#include <QWebView>

#include <QDebug>



MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui_(new Ui::MainWindow),
    tray_(this)
{
    ui_->setupUi(this);
    
    QWebSettings::globalSettings()->setAttribute(QWebSettings::PluginsEnabled, true);
    QWebSettings::globalSettings()->setAttribute(QWebSettings::JavascriptEnabled, true);
    QWebSettings::globalSettings()->setAttribute(QWebSettings::LocalStorageEnabled, true);
    QWebSettings::globalSettings()->setAttribute(QWebSettings::OfflineStorageDatabaseEnabled,true);
    QWebSettings::globalSettings()->setAttribute(QWebSettings::OfflineWebApplicationCacheEnabled,true);
    QWebSettings::globalSettings()->setAttribute(QWebSettings::DeveloperExtrasEnabled, true);
    QWebSettings::enablePersistentStorage("/tmp");

    QWebSettings::globalSettings()->setOfflineStorageDefaultQuota(5*1024*1024);
    QWebSettings::globalSettings()->setOfflineWebApplicationCacheQuota(5*1024*1024);


    QWebView* webEngine = new QWebView(centralWidget());
    
    p_.reset(new Plugin("yandex.js", webEngine));

    centralWidget()->layout()->addWidget(webEngine);
    webEngine->load(QUrl(p_->url()));
    connect(webEngine, SIGNAL(loadFinished(bool)), this, SLOT(loadFinished()));
    
    tray_.setIcon(QIcon::fromTheme("edit-undo"));
    tray_.setVisible(true);
    tray_.setContextMenu(new QMenu());
    
    createActions();
}

MainWindow::~MainWindow()
{}

void MainWindow::createActions()
{
    QMenu* menu = tray_.contextMenu();
    
    QSignalMapper* mapper = new QSignalMapper(p_.get());
    
    QAction* a = menu->addAction(QIcon::fromTheme("edit-undo"), "hello");
    connect(a, SIGNAL(triggered()), mapper, SLOT(map()));
    mapper->setMapping(a, "hello");
    
    connect(mapper, SIGNAL(mapped(const QString &)), p_.get(), SLOT(call(QString)));
}


void MainWindow::loadFinished()
{
    QWebView* webEngine = qobject_cast<QWebView*>(sender());
    p_->initialize(webEngine->page()->mainFrame());
}
