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
    ../../app/settings/streamingpreferences.cpp

HEADERS += \
    tests.h \
    ../../app/settings/streamingpreferences.h
