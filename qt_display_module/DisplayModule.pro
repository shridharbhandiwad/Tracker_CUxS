QT       += core gui widgets network
TARGET    = DisplayModule
TEMPLATE  = app
CONFIG   += c++17

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    udpreceiver.cpp \
    ppiwidget.cpp \
    scopewidget.cpp \
    timeserieswidget.cpp \
    logdialog.cpp

HEADERS += \
    mainwindow.h \
    udpreceiver.h \
    ppiwidget.h \
    scopewidget.h \
    timeserieswidget.h \
    logdialog.h

win32 {
    LIBS += -lws2_32
    DEFINES += WIN32_LEAN_AND_MEAN NOMINMAX
}
