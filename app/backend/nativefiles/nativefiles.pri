SOURCES += $$PWD/activity.cpp
HEADERS += $$PWD/activity.h
SOURCES += $$PWD/offerstore.cpp $$PWD/clipboardbroker.cpp $$PWD/jsontransport.cpp $$PWD/bridgehttp.cpp $$PWD/nativeagent.cpp
HEADERS += $$PWD/offerstore.h $$PWD/clipboardbroker.h $$PWD/jsontransport.h $$PWD/bridgehttp.h $$PWD/nativeagent.h $$PWD/nativeplatform.h $$PWD/materialize.h
macx {
    # ARC is required by this adapter only. Existing video/toolbar code uses MRC.
    DESK_MAC_NATIVE_SOURCE = $$PWD/platform_mac.mm
    desk_native_arc.input = DESK_MAC_NATIVE_SOURCE
    desk_native_arc.output = ${QMAKE_VAR_OBJECTS_DIR}desk_native_mac.o
    desk_native_arc.commands = $$QMAKE_CXX $(CXXFLAGS) $(INCPATH) -fobjc-arc -c ${QMAKE_FILE_IN} -o ${QMAKE_FILE_OUT}
    desk_native_arc.dependency_type = TYPE_C
    desk_native_arc.variable_out = OBJECTS
    QMAKE_EXTRA_COMPILERS += desk_native_arc
    LIBS += -framework AppKit -framework CoreServices
} else:win32 {
    SOURCES += $$PWD/platform_win.cpp
    LIBS += -lole32 -lshell32 -luuid -luser32
} else:unix {
    CONFIG += link_pkgconfig
    PKGCONFIG += fuse3
    SOURCES += $$PWD/platform_x11.cpp
}
HEADERS += $$PWD/processutils.h
