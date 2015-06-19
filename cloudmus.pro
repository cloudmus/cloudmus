#-------------------------------------------------
#
# Project created by QtCreator 2015-06-15T20:19:58
#
#-------------------------------------------------

QT += core gui webkit script

QMAKE_CXXFLAGS += -std=c++11

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets webkitwidgets
greaterThan(QT_MAJOR_VERSION, 4): CONFIG += c++11


TARGET = cloudmus
TEMPLATE = app

SOURCES += \
  main.cpp\
  mainwindow.cpp\
  service.cpp\
  service_manager.cpp\


HEADERS += \
  mainwindow.h\
  service.h\
  service_manager.h\
  tools.h\
 
RESOURCES += \
  images.qrc


FORMS += mainwindow.ui
