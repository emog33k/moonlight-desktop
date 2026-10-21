#include "tests.h"
#include "settings/streamingpreferences.h"
#include "streaming/localrdp/sessionmenuhost.h"

#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

using namespace LocalRdp;

namespace {

class FakeInput : public StreamMenuHost
{
public:
    StreamState state;
    QVector<StreamAction> performed;
    StreamState queryState() override { return state; }
    QVector<int> params;
    void performAction(StreamAction a, int param) override { performed << a; params << param; }
    bool isCaptureActive() override { return true; }
    void setCaptureActive(bool) override {}
    void suspendPointerLock(bool) override {}
};

}

static QTemporaryDir* s_Dir;

void TestSessionMenuHost::init()
{
    s_Dir = new QTemporaryDir();
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, s_Dir->path());
    StreamingPreferences::get()->reload();
}

void TestSessionMenuHost::cleanup()
{
    delete s_Dir;
    s_Dir = nullptr;
}

void TestSessionMenuHost::stateCombinesInputPrefsAndAgent()
{
    FakeInput input;
    input.state.fullscreen = true;
    StreamingPreferences::get()->lockOnDisconnect = true;
    SessionMenuHost host(&input, StreamingPreferences::get(), nullptr);

    const StreamState s = host.queryState();
    QCOMPARE(s.fullscreen, true);
    QCOMPARE(s.lockOnDisconnect, true);
    QCOMPARE(s.agentStatus, AgentStatus::NotFound); // no agent client at all
}

void TestSessionMenuHost::inputActionsForwarded()
{
    FakeInput input;
    SessionMenuHost host(&input, StreamingPreferences::get(), nullptr);
    host.performAction(StreamAction::Minimize);
    host.performAction(StreamAction::ToggleStats);
    QCOMPARE(input.performed, QVector<StreamAction>({ StreamAction::Minimize, StreamAction::ToggleStats }));
}

void TestSessionMenuHost::toggleLockOnDisconnectPersists()
{
    FakeInput input;
    StreamingPreferences* prefs = StreamingPreferences::get();
    SessionMenuHost host(&input, prefs, nullptr);

    QCOMPARE(host.queryState().lockOnDisconnect, false);
    host.performAction(StreamAction::ToggleLockOnDisconnect);
    QCOMPARE(host.queryState().lockOnDisconnect, true);

    prefs->lockOnDisconnect = false;
    prefs->reload();
    QCOMPARE(prefs->lockOnDisconnect, true); // saved
    QVERIFY(input.performed.isEmpty());      // not an input action
}

void TestSessionMenuHost::lockPcWithoutAgentIsHarmless()
{
    FakeInput input;
    SessionMenuHost host(&input, StreamingPreferences::get(), nullptr);
    host.performAction(StreamAction::LockPc);
    QVERIFY(input.performed.isEmpty());
}

void TestSessionMenuHost::toggleClipboardSyncPersists()
{
    FakeInput input;
    StreamingPreferences* prefs = StreamingPreferences::get();
    SessionMenuHost host(&input, prefs, nullptr);
    QCOMPARE(host.queryState().clipboardSync, true);
    host.performAction(StreamAction::ToggleClipboardSync);
    QCOMPARE(host.queryState().clipboardSync, false);
    prefs->clipboardSync = true;
    prefs->reload();
    QCOMPARE(prefs->clipboardSync, false);
    QVERIFY(input.performed.isEmpty());
}

void TestSessionMenuHost::sendFilesOpensPicker()
{
    FakeInput input;
    int picks = 0;
    SessionMenuHost host(&input, StreamingPreferences::get(), nullptr, [&]() { picks++; });
    host.performAction(StreamAction::SendFilesToPc);
    QCOMPARE(picks, 1);
    QVERIFY(input.performed.isEmpty());
}

void TestSessionMenuHost::switchDisplayForwardedAndRemembered()
{
    FakeInput input;
    SessionMenuHost host(&input, StreamingPreferences::get(), nullptr);
    QCOMPARE(host.queryState().currentDisplay, -1);
    host.performAction(StreamAction::SwitchDisplay, 1);
    QCOMPARE(input.performed, QVector<StreamAction>({ StreamAction::SwitchDisplay }));
    QCOMPARE(input.params, QVector<int>({ 1 }));
    QCOMPARE(host.queryState().currentDisplay, 1);
}

void TestSessionMenuHost::toggleCursorShapesPersists()
{
    FakeInput input;
    StreamingPreferences* prefs = StreamingPreferences::get();
    SessionMenuHost host(&input, prefs, nullptr);
    QCOMPARE(host.queryState().localCursorShapes, true);
    host.performAction(StreamAction::ToggleLocalCursorShapes);
    QCOMPARE(host.queryState().localCursorShapes, false);
    prefs->localCursorShapes = true;
    prefs->reload();
    QCOMPARE(prefs->localCursorShapes, false);
}
