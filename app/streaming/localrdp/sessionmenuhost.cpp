#include "sessionmenuhost.h"
#include "agentclient.h"
#include "settings/streamingpreferences.h"
#include "streamvolume.h"
#include "streammenumodel.h"
#include "localrdp-shared/desktoptr.h"

#include <QDateTime>

namespace LocalRdp {

SessionMenuHost::SessionMenuHost(StreamMenuHost* input, StreamingPreferences* prefs, AgentClient* agent,
                                 std::function<void()> pickFilesToSend)
    : m_Input(input),
      m_Prefs(prefs),
      m_Agent(agent),
      m_PickFilesToSend(std::move(pickFilesToSend))
{
}

StreamState SessionMenuHost::queryState()
{
    StreamState state = m_Input->queryState();
    state.agentStatus = m_Agent ? m_Agent->status() : AgentStatus::NotFound;
    state.lockOnDisconnect = m_Prefs->lockOnDisconnect;
    state.clipboardSync = m_Prefs->clipboardSync;
    state.localCursorShapes = m_Prefs->localCursorShapes;
    state.cursorSyncActive = m_CursorSyncActive ? m_CursorSyncActive() : false;
    state.streamAudio = m_Prefs->streamAudio;
    state.bitrateKbps = m_Prefs->bitrateKbps;
    state.fps = m_Prefs->fps;
    if (m_PowerConfirm != PowerConfirm::None && QDateTime::currentMSecsSinceEpoch() - m_PowerConfirmSince > 5000) {
        m_PowerConfirm = PowerConfirm::None;
    }
    state.powerConfirm = m_PowerConfirm;
    state.streamVolume = m_Prefs->streamVolume;
    state.currentDisplay = m_CurrentDisplay;
    if (m_Agent) {
        state.displays = m_Agent->displays();
        state.pcLocked = m_Agent->pcLocked();
        // Until the user picks one, the monitor Sunshine started on
        if (m_CurrentDisplay < 0) {
            state.currentDisplay = m_Agent->initialDisplay();
        }
    }
    return state;
}

void SessionMenuHost::performAction(StreamAction action, int param)
{
    switch (action) {
    case StreamAction::LockPc:
        if (m_Agent) {
            m_Agent->lockNow();
        }
        break;
    case StreamAction::ToggleLockOnDisconnect:
        m_Prefs->lockOnDisconnect = !m_Prefs->lockOnDisconnect;
        m_Prefs->saveInputPrefs();
        if (m_Agent) {
            m_Agent->setLockOnDisconnect(m_Prefs->lockOnDisconnect);
        }
        break;
    case StreamAction::ToggleClipboardSync:
        m_Prefs->clipboardSync = !m_Prefs->clipboardSync;
        m_Prefs->saveInputPrefs();
        if (m_Agent) {
            m_Agent->setClipboardSync(m_Prefs->clipboardSync);
        }
        break;
    case StreamAction::ToggleLocalCursorShapes:
        m_Prefs->localCursorShapes = !m_Prefs->localCursorShapes;
        m_Prefs->saveInputPrefs();
        if (m_OnCursorShapesChanged) {
            m_OnCursorShapesChanged(m_Prefs->localCursorShapes);
        }
        break;
    case StreamAction::SwitchDisplay:
        m_CurrentDisplay = param;
        m_Input->performAction(action, param);
        // Sunshine shows the new monitor only once something on it changes
        if (m_Agent) {
            m_Agent->refreshScreens();
        }
        break;
    case StreamAction::ToggleStreamAudio:
    case StreamAction::SetVolume:
        if (action == StreamAction::ToggleStreamAudio) {
            m_Prefs->streamAudio = !m_Prefs->streamAudio;
        }
        else {
            m_Prefs->streamVolume = qBound(0, param, 100);
        }
        m_Prefs->saveInputPrefs();
        setStreamAudio(m_Prefs->streamAudio, m_Prefs->streamVolume);
        break;
    case StreamAction::RebootPc:
    case StreamAction::ShutdownPc:
    {
        const PowerConfirm wanted = action == StreamAction::RebootPc ? PowerConfirm::Reboot : PowerConfirm::Shutdown;
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (m_PowerConfirm == wanted && now - m_PowerConfirmSince <= 5000) {
            m_PowerConfirm = PowerConfirm::None;
            if (m_Agent) {
                m_Agent->powerAction(wanted == PowerConfirm::Reboot ? QStringLiteral("reboot") : QStringLiteral("shutdown"));
                toast(wanted == PowerConfirm::Reboot ? dtr("The host is restarting…")
                                                     : dtr("The host is shutting down…"));
            }
        }
        else {
            m_PowerConfirm = wanted;
            m_PowerConfirmSince = now;
        }
        break;
    }
    case StreamAction::SetQuality:
    {
        const QVector<QualityPreset> presets = qualityPresets();
        if (param < 0 || param >= presets.size()) {
            break;
        }
        const QualityPreset& p = presets[param];
        if (m_Prefs->bitrateKbps == p.kbps && m_Prefs->fps == p.fps) {
            break;
        }
        m_Prefs->bitrateKbps = p.kbps;
        m_Prefs->fps = p.fps;
        m_Prefs->saveQualityPrefs();
        if (m_Restart) {
            toast(dtr("Reconnecting: %1…").arg(p.name));
            m_Restart();
        }
        break;
    }
    case StreamAction::FixDoubleCursor:
        if (m_FixDoubleCursor) {
            m_FixDoubleCursor();
        }
        break;
    case StreamAction::SnipLaptop:
        if (m_LaunchLaptopSnip) {
            m_LaunchLaptopSnip();
            toast(dtr("Select an area: the screenshot goes to the host clipboard (paste it there with Ctrl+V)"));
        }
        break;
    case StreamAction::SendKeys:
        m_Input->performAction(action, param);
        if (param == (int)QuickKeys::SnipArea) {
            toast(dtr("Select an area on the host: the screenshot comes to the clipboard here"));
        }
        else if (param == (int)QuickKeys::PrintScreen) {
            toast(dtr("Host screenshot is in the clipboard here"));
        }
        break;
    case StreamAction::SendFilesToPc:
        if (m_PickFilesToSend) {
            m_PickFilesToSend();
        }
        break;
    default:
        m_Input->performAction(action, param);
        break;
    }
}

bool SessionMenuHost::isCaptureActive()
{
    return m_Input->isCaptureActive();
}

void SessionMenuHost::setCaptureActive(bool active)
{
    m_Input->setCaptureActive(active);
}

void SessionMenuHost::suspendPointerLock(bool suspended)
{
    m_Input->suspendPointerLock(suspended);
}

}
