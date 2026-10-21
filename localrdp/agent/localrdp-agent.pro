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
DEPENDPATH = . ../../app/localrdp-shared

SOURCES += \
    main.cpp \
    agentserver.cpp \
    ../../app/localrdp-shared/protocol.cpp \
    ../../app/localrdp-shared/trust.cpp \
    ../../app/localrdp-shared/peerconnection.cpp

HEADERS += \
    agentserver.h \
    ../../app/localrdp-shared/protocol.h \
    ../../app/localrdp-shared/trust.h \
    ../../app/localrdp-shared/peerconnection.h

LIBS += crypt32.lib user32.lib
