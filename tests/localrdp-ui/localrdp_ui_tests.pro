# UI tests for the LocalRDP floating button and menu (real Win32 windows over a
# real SDL window, driven with synthetic mouse messages; the user's cursor is not touched).
QT += testlib
QT -= gui
CONFIG += console c++17 testcase
CONFIG -= app_bundle
TEMPLATE = app
TARGET = localrdp_ui_tests

DEFINES += SDL_MAIN_HANDLED
APP = ../../app
INCLUDEPATH += $$APP $$PWD/../../libs/windows/include/x64 $$PWD/../../libs/windows/include/x64/SDL2
LIBS += -L$$PWD/../../libs/windows/lib/x64 -lSDL2 gdiplus.lib user32.lib gdi32.lib

SOURCES += \
    main.cpp \
    tst_floatingbutton.cpp \
    tst_streammenu.cpp \
    $$APP/streaming/localrdp/buttonplacement.cpp \
    $$APP/streaming/localrdp/menulayout.cpp \
    $$APP/streaming/localrdp/streammenu_win32.cpp \
    $$APP/streaming/localrdp/streammenumodel.cpp \
    $$APP/streaming/localrdp/win32canvas.cpp \
    $$APP/streaming/localrdp/floatingbutton_win32.cpp \
    $$APP/streaming/localrdp/streammenucontroller.cpp

HEADERS += \
    uitests.h
