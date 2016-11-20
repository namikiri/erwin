QT += core network
QT -= gui

CONFIG += c++11

TARGET = Erwin
CONFIG += console
CONFIG -= app_bundle

TEMPLATE = app

SOURCES += main.cpp \
    erwin.cpp \
    galaxyhash.cpp

HEADERS += \
    erwin.h \
    galaxyhash.h

TRANSLATIONS += erwin_ru.ts

RC_FILE = erwin.rc
