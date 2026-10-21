# LocalRDP Agent: background program on the PC (no window), see main.cpp.
QT = core network
CONFIG += c++17 windows
CONFIG -= console app_bundle
TEMPLATE = app
TARGET = LocalRdpAgent

INCLUDEPATH += ../../app
# Track dependencies on our own headers only. With headers outside this folder,
# qmake otherwise writes broken relative paths to the Qt headers into the Makefile.
CONFIG -= depend_includepath
DEPENDPATH = . ../../app/localrdp-shared ../../app/localrdp-shared/clip

SOURCES += \
    main.cpp \
    agentserver.cpp     displays_win32.cpp     ../../app/localrdp-shared/hostdisplays.cpp \
    ../../app/localrdp-shared/protocol.cpp \
    ../../app/localrdp-shared/trust.cpp \
    ../../app/localrdp-shared/peerconnection.cpp \
    ../../app/localrdp-shared/clip/clipboardbridge_win32.cpp \
    ../../app/localrdp-shared/clip/clipexchange.cpp \
    ../../app/localrdp-shared/clip/clipmessages.cpp \
    ../../app/localrdp-shared/clip/clipreader_win32.cpp \
    ../../app/localrdp-shared/clip/remotedataobject_win32.cpp

HEADERS += \
    ../../app/localrdp-shared/clip/clipboardbridge_win32.h \
    ../../app/localrdp-shared/clip/clipexchange.h \
    ../../app/localrdp-shared/clip/clipmessages.h \
    ../../app/localrdp-shared/clip/clipreader_win32.h \
    ../../app/localrdp-shared/clip/remotedataobject_win32.h \
    agentserver.h     displays_win32.h     ../../app/localrdp-shared/hostdisplays.h \
    ../../app/localrdp-shared/protocol.h \
    ../../app/localrdp-shared/trust.h \
    ../../app/localrdp-shared/peerconnection.h

LIBS += crypt32.lib user32.lib ole32.lib shell32.lib uuid.lib dxgi.lib
