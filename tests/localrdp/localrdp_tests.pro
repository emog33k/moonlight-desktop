# Unit tests for the LocalRDP additions (pure logic, no SDL or Win32 window code).
# Build and run from a shell prepared by scripts\env.bat + vcvars:
#   cd tests\localrdp && qmake && nmake release && release\localrdp_tests.exe
QT += testlib qml network
RESOURCES += data/testdata.qrc
LIBS += crypt32.lib ole32.lib shell32.lib user32.lib psapi.lib
QT -= gui
CONFIG += console c++17 testcase
CONFIG -= app_bundle
TEMPLATE = app
TARGET = localrdp_tests

INCLUDEPATH += ../../app ../../localrdp/agent

SOURCES += \
    main.cpp \
    wmstub.cpp \
    tst_streamingpreferences.cpp \
    tst_streammenumodel.cpp \
    tst_buttonplacement.cpp \
    tst_menulayout.cpp \
    tst_protocol.cpp \
    tst_trust.cpp \
    tst_peerconnection.cpp \
    tst_agentserver.cpp \
    tst_agentclient.cpp \
    tst_sessionmenuhost.cpp \
    tst_clipmessages.cpp \
    tst_clipexchange.cpp \
    tst_clipreader.cpp \
    tst_remotedataobject.cpp \
    ../../app/localrdp-shared/clip/remotedataobject_win32.cpp \
    ../../app/localrdp-shared/clip/clipreader_win32.cpp \
    ../../app/localrdp-shared/clip/clipexchange.cpp \
    ../../app/localrdp-shared/clip/clipmessages.cpp \
    ../../app/streaming/localrdp/sessionmenuhost.cpp \
    ../../app/streaming/localrdp/agentclient.cpp \
    ../../localrdp/agent/agentserver.cpp \
    ../../app/localrdp-shared/protocol.cpp \
    ../../app/localrdp-shared/trust.cpp \
    ../../app/localrdp-shared/peerconnection.cpp \
    ../../app/streaming/localrdp/menulayout.cpp \
    ../../app/settings/streamingpreferences.cpp \
    ../../app/streaming/localrdp/streammenumodel.cpp \
    ../../app/streaming/localrdp/buttonplacement.cpp

HEADERS += \
    tests.h \
    ../../app/localrdp-shared/peerconnection.h \
    ../../localrdp/agent/agentserver.h \
    ../../app/streaming/localrdp/agentclient.h \
    ../../app/settings/streamingpreferences.h \
    ../../app/streaming/localrdp/streamactions.h \
    ../../app/streaming/localrdp/streammenumodel.h \
    ../../app/streaming/localrdp/buttonplacement.h
