QT += core gui network testlib
CONFIG += console testcase c++17
CONFIG -= app_bundle debug_and_release
TARGET = native-files-test
INCLUDEPATH += ../../app
SOURCES += tst_nativefiles.cpp
SOURCES += ../../app/backend/nativefiles/offerstore.cpp
SOURCES += ../../app/backend/nativefiles/clipboardbroker.cpp
HEADERS += ../../app/backend/nativefiles/clipboardbroker.h
SOURCES += ../../app/backend/nativefiles/jsontransport.cpp ../../app/backend/nativefiles/bridgehttp.cpp
win32-msvc*: QMAKE_CXXFLAGS += /utf-8

SOURCES += ../../app/backend/nativefiles/activity.cpp
