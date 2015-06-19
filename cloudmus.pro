#-------------------------------------------------
#
# Project created by QtCreator 2015-06-15T20:19:58
#
#-------------------------------------------------

QT += core gui webkit

QMAKE_CXXFLAGS += -std=c++11

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets webkitwidgets
greaterThan(QT_MAJOR_VERSION, 4): CONFIG += c++11
greaterThan(QT_MAJOR_VERSION, 4): DEFINES += HAVE_QT5


TARGET = cloudmus
TEMPLATE = app

SOURCES += \
  main.cpp\
  mainwindow.cpp\
  service_manager.cpp\
  service_descriptor.cpp\
  service.cpp\
  

HEADERS += \
  mainwindow.h\
  service_manager.h\
  service_descriptor.h\
  service.h\
  tools.h\
 
RESOURCES += \
  images.qrc


FORMS += mainwindow.ui
