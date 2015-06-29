
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
#include "aboutdialog.h"
#include "optionsdialog.h"

MainWindow::MainWindow(QWidget* parent) :
    QMainWindow(parent),
    ui_(new Ui::MainWindow),
    tray_(this)
{
    ui_->setupUi(this);

    QWebSettings::globalSettings()->setAttribute(QWebSettings::PluginsEnabled, true);
    QWebSettings::globalSettings()->setAttribute(QWebSettings::JavascriptEnabled, true);
    QWebSettings::globalSettings()->setAttribute(QWebSettings::JavaEnabled, true);
    QWebSettings::globalSettings()->setAttribute(QWebSettings::LocalStorageEnabled, true);
    QWebSettings::globalSettings()->setAttribute(QWebSettings::OfflineStorageDatabaseEnabled, true);
    QWebSettings::globalSettings()->setAttribute(QWebSettings::OfflineWebApplicationCacheEnabled, true);
    QWebSettings::globalSettings()->setAttribute(QWebSettings::DeveloperExtrasEnabled, true);
    QWebSettings::globalSettings()->setAttribute(QWebSettings::XSSAuditingEnabled, true);
    QWebSettings::globalSettings()->setThirdPartyCookiePolicy(QWebSettings::AlwaysAllowThirdPartyCookies);
    QWebSettings::enablePersistentStorage("/tmp");

    QWebSettings::globalSettings()->setOfflineStorageDefaultQuota(5 * 1024 * 1024);
    QWebSettings::globalSettings()->setOfflineWebApplicationCacheQuota(5 * 1024 * 1024);

    webEngine_ = new QWebView(centralWidget());
    centralWidget()->layout()->addWidget(webEngine_);

    tray_.setIcon(QIcon("icons:cloudmus.png"));
    tray_.setVisible(true);
    tray_.setContextMenu(new QMenu(this));
    titleAction_ = tray_.contextMenu()->addAction("");
    titleAction_->setEnabled(false);
    titleAction_->setVisible(false);
    QFont f = titleAction_->font();
    f.setBold(true);
    f.setPointSize(f.pointSize() + 2);
    titleAction_->setFont(f);

    servicesMenu_ = ui_->menuServices;
    tray_.contextMenu()->addMenu(servicesMenu_);

    ServiceManager manager;
    services_ = manager.list();

    for (auto p : services_)
        addService(p);

}

MainWindow::~MainWindow()
{
    delete ui_;
}

QString fallbackIcon(QString icon)
{
    const static std::map<QString, QString> fallbackIcons = {
        {"play",  "media-playback-start"},
        {"stop",  "media-playback-stop"},
        {"pause", "media-playback-pause"},
        {"next",  "media-skip-forward"},
    };

    const auto it = fallbackIcons.find(icon);
    return (it == fallbackIcons.end()) ? icon : it->second;
}

void MainWindow::addService(ServiceDescriptorPtr service)
{
    QAction* a = servicesMenu_->addAction(service->icon("preferences-plugin"), service->name());
    ::connect(a, SIGNAL(triggered(bool)), [this, service, a]() {
        activateService(service);
    });
}

void MainWindow::activateService(ServiceDescriptorPtr service)
{
    titleAction_->setText("");
    titleAction_->setVisible(false);
    if (current_) {
        actions_.clear();
        current_->destroy();
        current_.reset();
    }

    if (service.get()) {
        current_ = service;
        current_->create();

        titleAction_->setText(current_->name());
        titleAction_->setVisible(true);

        Q_VERIFY(connect(current_->service(), SIGNAL(addAction(QAction*)), this, SLOT(addAction(QAction*))));
        current_->service()->initialize(webEngine_->page()->mainFrame());
    }
}

void MainWindow::addAction(QAction* action)
{
    tray_.contextMenu()->addAction(action);
}

void MainWindow::on_actionABout_triggered()
{
    AboutDialog dlg;
    dlg.exec();
}

void MainWindow::on_actionSettings_triggered()
{
    OptionsDialog dlg;
    dlg.exec();
}
