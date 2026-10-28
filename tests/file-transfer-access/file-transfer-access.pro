QT += core testlib
QT -= gui
CONFIG += console testcase c++17
CONFIG -= app_bundle debug_and_release
TARGET = file-transfer-access-test
win32-msvc*: QMAKE_CXXFLAGS += /utf-8
INCLUDEPATH += $$JSON_INCLUDE
SOURCES += tst_fileaccess.cpp
macx {
    INCLUDEPATH += ../../libs/mac/include
    LIBS += -L$$PWD/../../libs/mac/lib -lcrypto.3
    QMAKE_RPATHDIR += $$PWD/../../libs/mac/lib
} else:win32 {
    SDL_ARCH = x64
    contains(QT_ARCH, arm64): SDL_ARCH = arm64
    INCLUDEPATH += $$PWD/../../libs/windows/include $$PWD/../../libs/windows/include/$$SDL_ARCH
    LIBS += -L$$PWD/../../libs/windows/lib/$$SDL_ARCH -llibcrypto
} else {
    CONFIG += link_pkgconfig
    PKGCONFIG += openssl
}
