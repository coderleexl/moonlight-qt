QT += core gui network testlib
CONFIG += console testcase c++17
CONFIG -= app_bundle debug_and_release
TARGET = clipboard-agent-test
INCLUDEPATH += ../../app
SOURCES += tst_clipboardagent.cpp fakeplatform.cpp \
    ../../app/backend/identitymanager.cpp \
    ../../app/backend/nativefiles/nativeagent.cpp \
    ../../app/backend/nativefiles/offerstore.cpp \
    ../../app/backend/nativefiles/clipboardbroker.cpp \
    ../../app/backend/nativefiles/jsontransport.cpp \
    ../../app/backend/nativefiles/bridgehttp.cpp \
    ../../app/backend/nativefiles/activity.cpp
HEADERS += fakestate.h \
    ../../app/backend/nativefiles/nativeplatform.h \
    ../../app/backend/nativefiles/clipboardbroker.h
macx {
    INCLUDEPATH += ../../libs/mac/include
    LIBS += -L$$PWD/../../libs/mac/lib -lssl.3 -lcrypto.3
    QMAKE_RPATHDIR += $$PWD/../../libs/mac/lib
} else {
    CONFIG += link_pkgconfig
    PKGCONFIG += openssl
}
