#pragma once

#include <QObject>

// Each test class is declared here and run from main.cpp, so all LocalRDP
// tests build into a single executable.

class TestStreamingPreferences : public QObject
{
    Q_OBJECT
private slots:
    void init();
    void cleanup();
    void remoteDesktopDefaults();
    void newPreferencesRoundTrip();
};

class TestStreamMenuModel : public QObject
{
    Q_OBJECT
private slots:
    void headersInOrder();
    void desktopModeChecked();
    void checksMirrorState();
    void everyActionableItemHasAction();
    void everyActionAppearsOnce();
    void shortcutText();
};
