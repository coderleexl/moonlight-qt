SOURCES += $$PWD/activity.cpp
HEADERS += $$PWD/activity.h
SOURCES += $$PWD/offerstore.cpp $$PWD/clipboardbroker.cpp $$PWD/jsontransport.cpp $$PWD/bridgehttp.cpp $$PWD/nativeagent.cpp
HEADERS += $$PWD/offerstore.h $$PWD/clipboardbroker.h $$PWD/jsontransport.h $$PWD/bridgehttp.h $$PWD/nativeagent.h $$PWD/nativeplatform.h $$PWD/materialize.h
macx {
    OBJECTIVE_SOURCES += $$PWD/platform_mac.mm
    QMAKE_OBJECTIVE_CFLAGS += -fobjc-arc
    QMAKE_OBJECTIVE_CXXFLAGS += -fobjc-arc
    LIBS += -framework AppKit -framework CoreServices
} else:win32 {
    SOURCES += $$PWD/platform_win.cpp
    LIBS += -lole32 -lshell32 -luuid
} else:unix {
    CONFIG += link_pkgconfig
    PKGCONFIG += fuse3
    SOURCES += $$PWD/platform_x11.cpp
}
HEADERS += $$PWD/processutils.h
