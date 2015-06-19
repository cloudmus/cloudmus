#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <memory>

#include <QMainWindow>
#include <QSystemTrayIcon>

#include "service.h"

namespace Ui {
    class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = 0);
    ~MainWindow();

public Q_SLOTS:
    void addService(Service_p service);
    void activateService(Service_p service);
    void addAction(QString text, QString icon, QString action);
    
Q_SIGNALS:
    void finalized(Service_p plugin);
    
    
private:

  
private:
    std::unique_ptr<Ui::MainWindow> ui_;
    QSystemTrayIcon tray_;
    
    QList<Service_p> services_;
    QMenu* services_menu_;
    
    Service_p s_;
    QAction* title_action_;

};

#endif // MAINWINDOW_H
