#include "tests.h"
#include "settings/streamingpreferences.h"

#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

static QTemporaryDir* s_SettingsDir;

void TestStreamingPreferences::init()
{
    // Fresh, empty settings for every test (what a new portable install sees).
    s_SettingsDir = new QTemporaryDir();
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, s_SettingsDir->path());
}

void TestStreamingPreferences::cleanup()
{
    delete s_SettingsDir;
    s_SettingsDir = nullptr;
}

void TestStreamingPreferences::remoteDesktopDefaults()
{
    // The constructor is private; the app uses the singleton and reload().
    StreamingPreferences* prefs = StreamingPreferences::get();
    prefs->reload();

    QCOMPARE(prefs->absoluteMouseMode, true);
    QCOMPARE(prefs->showLocalCursor, false);
    QCOMPARE(prefs->lockPointerToWindow, false);
    QCOMPARE(prefs->captureSysKeysMode, StreamingPreferences::CSK_ALWAYS);
    QCOMPARE(prefs->windowMode, StreamingPreferences::WM_WINDOWED);
    QCOMPARE(prefs->lockOnDisconnect, false);
    QCOMPARE(prefs->clipboardSync, true);
}

void TestStreamingPreferences::newPreferencesRoundTrip()
{
    StreamingPreferences* prefs = StreamingPreferences::get();
    prefs->reload();
    prefs->showLocalCursor = true;
    prefs->lockPointerToWindow = true;
    prefs->save();

    prefs->showLocalCursor = false;
    prefs->lockPointerToWindow = false;
    prefs->reload();

    QCOMPARE(prefs->showLocalCursor, true);
    QCOMPARE(prefs->lockPointerToWindow, true);
}

void TestStreamingPreferences::saveInputPrefsOnlyWritesInputKeys()
{
    StreamingPreferences* prefs = StreamingPreferences::get();
    prefs->reload();
    const int defaultWidth = prefs->width;

    prefs->absoluteMouseMode = false;
    prefs->showLocalCursor = true;
    prefs->lockPointerToWindow = true;
    prefs->captureSysKeysMode = StreamingPreferences::CSK_OFF;
    prefs->lockOnDisconnect = true;
    prefs->clipboardSync = false;
    prefs->width = 1234; // e.g. a one-off command line override; must not be persisted
    prefs->saveInputPrefs();

    prefs->reload();
    QCOMPARE(prefs->absoluteMouseMode, false);
    QCOMPARE(prefs->showLocalCursor, true);
    QCOMPARE(prefs->lockPointerToWindow, true);
    QCOMPARE(prefs->captureSysKeysMode, StreamingPreferences::CSK_OFF);
    QCOMPARE(prefs->lockOnDisconnect, true);
    QCOMPARE(prefs->clipboardSync, false);
    QCOMPARE(prefs->width, defaultWidth);
}
