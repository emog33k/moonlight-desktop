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
    void agentStatusLine();
    void lockPcNeedsAgent();
    void lockPcTexts();
    void sendFilesNeedsAgentAndSync();
    void sectionsSplit();
    void displaysListed();
    void displaysUnknownGivesGeneric();
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
    void statusRowAndDisabledItems();
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
    void utf8BomTolerated();
};

class TestPeerConnection : public QObject
{
    Q_OBJECT
private slots:
    void pinnedServerConnects();
    void wrongServerCertRejected();
    void garbageFrameCloses();
};

class TestAgentServer : public QObject
{
    Q_OBJECT
private slots:
    void helloAck();
    void unpairedClientDropped();
    void lockNow();
    void goodbyeLocksWhenEnabled();
    void goodbyeNoLockWhenDisabled();
    void dropLocksAfterGrace();
    void reconnectWithinGraceCancelsLock();
    void unpairWhileRunning();
    void silentClientDroppedAndLocks();
    void pingsKeepSessionAlive();
    void reconnectThenDropStillLocks();
};

class TestAgentClient : public QObject
{
    Q_OBJECT
private slots:
    void connectsAndReportsConnected();
    void notFoundWhenNoServer();
    void certErrorWhenServerCertDiffers();
    void rejectedWhenNotPaired();
    void lockNowReachesServer();
    void goodbyeWithLockOnDisconnect();
    void reconnectsAfterAgentRestart();
    void destructorReturnsFast();
    void deadServerDetected();
    void liveSessionStaysConnected();
    void filesOfferedFromLaptopReachThePc();
    void clipboardSyncOffSendsNothing();
};

class TestSessionMenuHost : public QObject
{
    Q_OBJECT
private slots:
    void init();
    void cleanup();
    void stateCombinesInputPrefsAndAgent();
    void inputActionsForwarded();
    void toggleLockOnDisconnectPersists();
    void lockPcWithoutAgentIsHarmless();
    void toggleClipboardSyncPersists();
    void sendFilesOpensPicker();
    void switchDisplayForwardedAndRemembered();
    void toggleCursorShapesPersists();
};

class TestClipMessages : public QObject
{
    Q_OBJECT
private slots:
    void offerRoundTrip();
    void emptyOffer();
    void unsafeNamesRejected();
    void safeNames();
    void limits();
    void requestRoundTrip();
    void chunkRoundTrip();
    void chunkTooShortRejected();
    void errorRoundTrip();
};

class TestClipExchange : public QObject
{
    Q_OBJECT
private slots:
    void textRoundTrip();
    void fileRangeFromDisk();
    void staleSeqErrors();
    void newOfferCancelsWait();
    void disconnectCancelsWait();
    void timeoutReturnsEmpty();
    void directoryIndexRejected();
    void indexOutOfRangeRejected();
    void clearedSourceErrors();
    void oldRequestAfterNewOfferFails();
    void moreDataThanOfferedFails();
};

class TestClipReader : public QObject
{
    Q_OBJECT
private slots:
    void textOffer();
    void dibOffer();
    void pngOffer();
    void filesAndNestedFolders();
    void excludedFormatMarksExcluded();
    void historyZeroMarksExcluded();
    void tooManyFilesGivesNoFiles();
};

class TestRemoteDataObject : public QObject
{
    Q_OBJECT
private slots:
    void enumFormats();
    void textGetData();
    void dibGetData();
    void fileDescriptorNamesSizes();
    void fileContentsStreamMatchesSource();
    void largeStreamBoundedMemory();
    void historyExclusionFormats();
    void fetchFailureGivesError();
    void oldStreamFailsAfterNewOffer();
    void textCachedAcrossGetData();
};

class TestClipboardBridge : public QObject
{
    Q_OBJECT
private slots:
    void offerFilesSendsOffer();
    void peerRequestServedFromOfferFiles();
    void disabledSendsNothing();
    void notConnectedSendsNothing();
    void badFramesIgnored();
    void systemClipboardRoundTrip();
    void fileRequestsServedOffCallerThread();
    void offerFilesReportsOutcome();
    void seqStartsRandom();
};

class TestVideoArea : public QObject
{
    Q_OBJECT
private slots:
    void exactFit();
    void pillarbox();
    void toolbarInsetKeepsVideoBelow();
    void tinyOrBrokenInputs();
    void insetSetting();
};

class TestToolbarLayout : public QObject
{
    Q_OBJECT
private slots:
    void buttonsAreSectionsThenActions();
    void leftToRightAndRightAligned();
    void narrowWindowKeepsRightButtons();
    void scalesWithDpi();
    void hitTest();
};
