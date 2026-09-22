QT += core network gui testlib
CONFIG += console testcase c++17
CONFIG -= app_bundle
TARGET = sunshine-manager-test
SOURCES += tst_sunshinemanager.cpp ../../app/backend/sunshinemanager.cpp
HEADERS += ../../app/backend/sunshinemanager.h
INCLUDEPATH += ../../app/backend

host_preview: DEFINES += MOONLIGHT_HOST_PREVIEW
