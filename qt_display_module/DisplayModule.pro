QT       += core gui widgets network
TARGET    = DisplayModule
TEMPLATE  = app
CONFIG   += c++17

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    udpreceiver.cpp

HEADERS += \
    mainwindow.h \
    udpreceiver.h

win32 {
    LIBS += -lws2_32
    DEFINES += WIN32_LEAN_AND_MEAN NOMINMAX
}
