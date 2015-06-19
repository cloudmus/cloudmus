#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <memory>

#include <QMainWindow>
#include <QSystemTrayIcon>

#include "plugin.h"

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
    void addAction(QString text, QString icon, QString action);
    
private:

  
private:
    std::unique_ptr<Ui::MainWindow> ui_;
    QSystemTrayIcon tray_;
    std::shared_ptr<Plugin> p_;
};

#endif // MAINWINDOW_H
