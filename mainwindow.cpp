
#include <QDebug>
#include <QFile>
#include <QMenu>
#include <QSignalMapper>
#include <QWebFrame>
#include <QWebInspector>
#include <QWebSettings>
#include <QWebView>

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
    QWebSettings::globalSettings()->setAttribute(QWebSettings::LocalStorageEnabled, true);
    QWebSettings::globalSettings()->setAttribute(QWebSettings::OfflineStorageDatabaseEnabled,true);
    QWebSettings::globalSettings()->setAttribute(QWebSettings::OfflineWebApplicationCacheEnabled,true);
    QWebSettings::globalSettings()->setAttribute(QWebSettings::DeveloperExtrasEnabled, true);
    QWebSettings::enablePersistentStorage("/tmp");

    QWebSettings::globalSettings()->setOfflineStorageDefaultQuota(5*1024*1024);
    QWebSettings::globalSettings()->setOfflineWebApplicationCacheQuota(5*1024*1024);


    QWebView* webEngine = new QWebView(centralWidget());
    centralWidget()->layout()->addWidget(webEngine);
    
    tray_.setIcon(QIcon::fromTheme("edit-undo"));
    tray_.setVisible(true);
    tray_.setContextMenu(new QMenu());
    
    p_.reset(new Plugin("yandex.js", webEngine));
    Q_VERIFY(connect(p_.get(), SIGNAL(addActionSignal(QString, QString, QString)), this, SLOT(addAction(QString, QString, QString))));
    p_->initialize(webEngine->page()->mainFrame());
}

MainWindow::~MainWindow()
{}

void MainWindow::addAction(QString text, QString icon, QString callback)
{
    QMenu* menu = tray_.contextMenu();
    QSignalMapper* mapper = new QSignalMapper(p_.get());
    QAction* a = menu->addAction(QIcon::fromTheme(icon), text);
    Q_VERIFY(connect(a, SIGNAL(triggered()), mapper, SLOT(map())));
    mapper->setMapping(a, callback);
    Q_VERIFY(connect(mapper, SIGNAL(mapped(const QString &)), p_.get(), SLOT(call(QString))));
}

