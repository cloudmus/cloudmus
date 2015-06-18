#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QFile>

#include <QWebView>
#include <QWebFrame>
#include <QWebSettings>
#include <QWebInspector>

#include <QDebug>

#include <plugin.h>

Plugin* p;

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    ui->setupUi(this);
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
    p = new Plugin("yandex.js", webEngine);

    centralWidget()->layout()->addWidget(webEngine);
    webEngine->setUrl(QUrl(p->url()));
    connect(webEngine, SIGNAL(urlChanged(QUrl)), this, SLOT(loadFinished()));
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::loadFinished()
{
    QWebView* webEngine = qobject_cast<QWebView*>(sender());
    webEngine->page()->mainFrame()->evaluateJavaScript("console.log('loadFinished1');");
    // webEngine->page()->currentFrame()->addToJavaScriptWindowObject ("WebMusicPlugin", p);
    webEngine->page()->mainFrame()->evaluateJavaScript("console.log('loadFinished2');");

    // p->hello(webEngine->page()->currentFrame());

    // QFile file("yandex.js");
    // file.open(QFile::ReadOnly);

    // webEngine->page()->currentFrame()->evaluateJavaScript(file.readAll());
    // webEngine->page()->currentFrame()->evaluateJavaScript("console.log(window.WebMusicPlugin);");

}
