#pragma once

#include <memory>

#include <QMainWindow>
#include <QPointer>
#include <QSystemTrayIcon>

#include "service_descriptor.h"

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

private:
    std::unique_ptr<Ui::MainWindow> ui_;
    QSystemTrayIcon tray_;

    QList<ServiceDescriptorPtr> services_;
    QMenu* servicesMenu_;

    ServiceDescriptorPtr current_;
    QAction* titleAction_;

    QMap<QString, QPointer<QAction>> actions_;
};
