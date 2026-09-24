QT += core network gui testlib
CONFIG += console testcase c++17
CONFIG -= app_bundle
TARGET = sunshine-manager-test
SOURCES += tst_sunshinemanager.cpp ../../app/backend/sunshinemanager.cpp
HEADERS += ../../app/backend/sunshinemanager.h
INCLUDEPATH += ../../app/backend ../../app

host_preview: DEFINES += MOONLIGHT_HOST_PREVIEW

include(../../app/backend/nativefiles/nativefiles.pri)
SOURCES += ../../app/backend/identitymanager.cpp

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
