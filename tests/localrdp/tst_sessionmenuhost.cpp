#include "tests.h"
#include "settings/streamingpreferences.h"
#include "streaming/localrdp/sessionmenuhost.h"
#include "streaming/localrdp/streamvolume.h"
#include "streaming/localrdp/streammenumodel.h"

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

void TestSessionMenuHost::quickActions()
{
    FakeInput input;
    SessionMenuHost host(&input, StreamingPreferences::get(), nullptr);
    int snips = 0;
    QStringList toasts;
    host.setLaptopSnipLauncher([&snips]() { snips++; });
    host.setToastHandler([&toasts](const QString& t) { toasts << t; });

    host.performAction(StreamAction::SnipLaptop);
    QCOMPARE(snips, 1);
    QCOMPARE(toasts.size(), 1); // tells where the picture goes
    QVERIFY(input.performed.isEmpty());

    host.performAction(StreamAction::SendKeys, (int)QuickKeys::SnipArea);
    QCOMPARE(input.performed, QVector<StreamAction>({ StreamAction::SendKeys }));
    QCOMPARE(input.params, QVector<int>({ (int)QuickKeys::SnipArea }));
    QCOMPARE(toasts.size(), 2);
    host.performAction(StreamAction::SendKeys, (int)QuickKeys::TaskManager);
    QCOMPARE(toasts.size(), 2); // plain keys: no notice
}

void TestSessionMenuHost::doubleCursorFixForwarded()
{
    FakeInput input;
    SessionMenuHost host(&input, StreamingPreferences::get(), nullptr);
    int fixes = 0;
    bool active = false;
    host.setFixDoubleCursorHandler([&fixes]() { fixes++; });
    host.setCursorSyncActiveProvider([&active]() { return active; });
    QCOMPARE(host.queryState().cursorSyncActive, false);
    active = true;
    QCOMPARE(host.queryState().cursorSyncActive, true);
    host.performAction(StreamAction::FixDoubleCursor);
    QCOMPARE(fixes, 1);
    QVERIFY(input.performed.isEmpty());
}

void TestSessionMenuHost::soundActions()
{
    FakeInput input;
    StreamingPreferences* prefs = StreamingPreferences::get();
    SessionMenuHost host(&input, prefs, nullptr);
    QCOMPARE(host.queryState().streamAudio, true);
    QCOMPARE(host.queryState().streamVolume, 100);

    host.performAction(StreamAction::VolumeDown);
    host.performAction(StreamAction::VolumeDown);
    QCOMPARE(host.queryState().streamVolume, 80);
    QCOMPARE(streamVolume(), 80); // what the audio thread uses
    host.performAction(StreamAction::ToggleStreamAudio);
    QCOMPARE(host.queryState().streamAudio, false);
    QCOMPARE(streamAudioOn(), false);
    for (int i = 0; i < 15; i++) {
        host.performAction(StreamAction::VolumeUp);
    }
    QCOMPARE(host.queryState().streamVolume, 100);
    QVERIFY(input.performed.isEmpty());

    // Remembered
    prefs->streamAudio = true;
    prefs->streamVolume = 1;
    prefs->reload();
    QCOMPARE(prefs->streamAudio, false);
    QCOMPARE(prefs->streamVolume, 100);
    host.performAction(StreamAction::ToggleStreamAudio);
}

void TestSessionMenuHost::qualityChangeReconnects()
{
    FakeInput input;
    StreamingPreferences* prefs = StreamingPreferences::get();
    SessionMenuHost host(&input, prefs, nullptr);
    int restarts = 0;
    host.setRestartHandler([&restarts]() { restarts++; });
    const QVector<QualityPreset> presets = qualityPresets();
    prefs->bitrateKbps = presets[0].kbps;
    prefs->fps = presets[0].fps;

    host.performAction(StreamAction::SetQuality, 0); // already that: nothing
    QCOMPARE(restarts, 0);
    const int width = prefs->width;
    prefs->width = 1234; // a one-off override must not be saved along
    host.performAction(StreamAction::SetQuality, 2);
    QCOMPARE(restarts, 1);
    QCOMPARE(prefs->bitrateKbps, presets[2].kbps);
    QCOMPARE(prefs->fps, presets[2].fps);
    prefs->reload();
    QCOMPARE(prefs->bitrateKbps, presets[2].kbps); // remembered
    QCOMPARE(prefs->fps, presets[2].fps);
    QCOMPARE(prefs->width, width);
    host.performAction(StreamAction::SetQuality, 99); // unknown: nothing
    QCOMPARE(restarts, 1);
}
