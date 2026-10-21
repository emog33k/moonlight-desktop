# Unit tests for the LocalRDP additions (pure logic, no SDL or Win32 window code).
# Build and run from a shell prepared by scripts\env.bat + vcvars:
#   cd tests\localrdp && qmake && nmake release && release\localrdp_tests.exe
QT += testlib qml
QT -= gui
CONFIG += console c++17 testcase
CONFIG -= app_bundle
TEMPLATE = app
TARGET = localrdp_tests

INCLUDEPATH += ../../app

SOURCES += \
    main.cpp \
    wmstub.cpp \
    tst_streamingpreferences.cpp \
    tst_streammenumodel.cpp \
    tst_buttonplacement.cpp \
    tst_menulayout.cpp \
    tst_protocol.cpp \
    ../../app/localrdp-shared/protocol.cpp \
    ../../app/streaming/localrdp/menulayout.cpp \
    ../../app/settings/streamingpreferences.cpp \
    ../../app/streaming/localrdp/streammenumodel.cpp \
    ../../app/streaming/localrdp/buttonplacement.cpp

HEADERS += \
    tests.h \
    ../../app/settings/streamingpreferences.h \
    ../../app/streaming/localrdp/streamactions.h \
    ../../app/streaming/localrdp/streammenumodel.h \
    ../../app/streaming/localrdp/buttonplacement.h
