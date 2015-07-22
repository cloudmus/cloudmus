#pragma once

#include <memory>

#include <QMainWindow>
#include <QPointer>
#include <QSystemTrayIcon>

#include "service_descriptor.h"

class QWebView;

namespace Ui
{
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = 0);
    ~MainWindow();

public Q_SLOTS:
    void addService(ServiceDescriptorPtr service);
    void activateService(ServiceDescriptorPtr service);
    void addAction(QAction* action);

private slots:
    void on_actionAbout_triggered();
    void on_actionSettings_triggered();
    void on_actionExit_triggered();
    void trayActivated(QSystemTrayIcon::ActivationReason reason);

private:
    Ui::MainWindow* ui_;
    QSystemTrayIcon tray_;

    QList<ServiceDescriptorPtr> services_;
    QMenu* servicesMenu_;

    ServiceDescriptorPtr current_;
    QAction* titleAction_;

    QMap<QString, QPointer<QAction>> actions_;
    QWebView* webEngine_;
};
