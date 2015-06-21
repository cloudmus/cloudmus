#ifndef MAINWINDOW_H
#define MAINWINDOW_H

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
    void addService(ServiceDescriptor_p service);
    void activateService(ServiceDescriptor_p service);
    void addAction(QAction* action);

private:


private:
    std::unique_ptr<Ui::MainWindow> ui_;
    QSystemTrayIcon tray_;

    QList<ServiceDescriptor_p> services_;
    QMenu* services_menu_;

    ServiceDescriptor_p current_;
    QAction* title_action_;

    QMap<QString, QPointer<QAction>> actions_;

};

#endif // MAINWINDOW_H
