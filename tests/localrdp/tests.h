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
    void saveInputPrefsOnlyWritesInputKeys();
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

class TestButtonPlacement : public QObject
{
    Q_OBJECT
private slots:
    void rightEdgeMiddle();
    void leftEdgeTop();
    void bottomEdgeEnd();
    void topEdgeStart();
    void fractionClamped();
    void tinyWindow();
    void snapRight();
    void snapTop();
    void snapRoundTrip();
    void clickVsDrag();
    void persistRoundTrip();
    void corruptSettingsFallBack();
};

class TestMenuLayout : public QObject
{
    Q_OBJECT
private slots:
    void rowsStackAtScale1();
    void scalesWithDpi();
    void hitTestOnlyActionable();
    void keyboardNavigationSkipsNonActionable();
    void menuLeftOfRightEdgeButton();
    void menuRightOfLeftEdgeButton();
    void menuBelowTopEdgeButton();
    void menuAboveBottomEdgeButton();
    void menuKeptInsideWorkArea();
};

class TestProtocol : public QObject
{
    Q_OBJECT
private slots:
    void roundTrip();
    void emptyPayload();
    void splitAcrossFeeds();
    void twoFramesInOneFeed();
    void oversizeFails();
    void zeroLengthFails();
    void badJsonRejected();
};

class TestTrust : public QObject
{
    Q_OBJECT
private slots:
    void pairedEnabledAccepted();
    void pairedDisabledRejected();
    void unknownRejected();
    void nullCertRejected();
    void garbageJsonRejected();
    void missingRootRejected();
    void dpapiRoundTrip();
    void dpapiTamperedFails();
};
