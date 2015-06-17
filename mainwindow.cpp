#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QWebView>
#include <QWebSettings>

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    ui->setupUi(this);
//    ui->webView->setUrl(QUrl("https://music.yandex.ru/"));
    QWebSettings::globalSettings()->setAttribute(QWebSettings::PluginsEnabled, true);
    QWebSettings::globalSettings()->setAttribute(QWebSettings::JavascriptEnabled, true);
    QWebSettings::globalSettings()->setAttribute(QWebSettings::DeveloperExtrasEnabled, true);
    QWebSettings::globalSettings()->setAttribute(QWebSettings::LocalStorageEnabled, true);

    QWebView* webEngine = new QWebView(centralWidget());


    centralWidget()->layout()->addWidget(webEngine);
    webEngine->load(QUrl("https://music.yandex.ru/"));

}

MainWindow::~MainWindow()
{
    delete ui;
}
