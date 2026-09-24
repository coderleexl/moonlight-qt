QT += core gui network quick quickcontrols2 testlib svg
CONFIG += console testcase c++17
CONFIG -= app_bundle
TARGET = file-transfer-test
win32-msvc*: QMAKE_CXXFLAGS += /utf-8
# Sunshine's src/process.h shadows the Windows CRT header if src is on /I.
INCLUDEPATH += ../../app $$JSON_INCLUDE
SOURCES += tst_filetransfer.cpp ../../app/backend/filetransfer.cpp ../../app/backend/identitymanager.cpp
HEADERS += ../../app/backend/filetransfer.h
macx {
    INCLUDEPATH += ../../libs/mac/include
    LIBS += -L$$PWD/../../libs/mac/lib -lssl.3 -lcrypto.3
    QMAKE_RPATHDIR += $$PWD/../../libs/mac/lib
} else:win32 {
    SDL_ARCH = x64
    contains(QT_ARCH, arm64): SDL_ARCH = arm64
    INCLUDEPATH += $$PWD/../../libs/windows/include $$PWD/../../libs/windows/include/$$SDL_ARCH
    LIBS += -L$$PWD/../../libs/windows/lib/$$SDL_ARCH -llibssl -llibcrypto
} else {
    CONFIG += link_pkgconfig
    PKGCONFIG += openssl
}

RESOURCES += filetransfer.qrc

include(../../app/backend/nativefiles/nativefiles.pri)
DEFINES += DESK_NATIVE_TESTS
