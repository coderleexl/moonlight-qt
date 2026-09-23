QT += core gui testlib
CONFIG += console c++17 testcase
CONFIG -= app_bundle
DEFINES += SDL_MAIN_HANDLED
TEMPLATE = app
TARGET = stream-toolbar-test
INCLUDEPATH += ../../app
SOURCES += tst_streamtoolbar.cpp ../../app/streaming/streamtoolbar.cpp
HEADERS += ../../app/streaming/streamtoolbar.h
macx {
    INCLUDEPATH += ../../libs/mac/include/SDL2
    LIBS += -L$$PWD/../../libs/mac/lib -lSDL2 -framework Cocoa -framework QuartzCore
    QMAKE_RPATHDIR += $$PWD/../../libs/mac/lib
    SOURCES += ../../app/streaming/streamtoolbar_mac.mm
    SOURCES += macfixture.mm
} else:win32 {
    SDL_ARCH = x64
    contains(QT_ARCH, arm64): SDL_ARCH = arm64
    INCLUDEPATH += $$PWD/../../libs/windows/include/$$SDL_ARCH/SDL2
    LIBS += -L$$PWD/../../libs/windows/lib/$$SDL_ARCH -lSDL2
} else {
    CONFIG += link_pkgconfig
    PKGCONFIG += sdl2
}
