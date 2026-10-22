#include "sessionmenuhost.h"
#include "agentclient.h"
#include "settings/streamingpreferences.h"
#include "streamvolume.h"

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
    case StreamAction::VolumeUp:
    case StreamAction::VolumeDown:
        if (action == StreamAction::ToggleStreamAudio) {
            m_Prefs->streamAudio = !m_Prefs->streamAudio;
        }
        else {
            m_Prefs->streamVolume = qBound(0, m_Prefs->streamVolume + (action == StreamAction::VolumeUp ? 10 : -10), 100);
        }
        m_Prefs->saveInputPrefs();
        setStreamAudio(m_Prefs->streamAudio, m_Prefs->streamVolume);
        break;
    case StreamAction::FixDoubleCursor:
        if (m_FixDoubleCursor) {
            m_FixDoubleCursor();
        }
        break;
    case StreamAction::SnipLaptop:
        if (m_LaunchLaptopSnip) {
            m_LaunchLaptopSnip();
            toast(QStringLiteral("Выделите область — снимок окажется в буфере ПК (вставьте там Ctrl+V)"));
        }
        break;
    case StreamAction::SendKeys:
        m_Input->performAction(action, param);
        if (param == (int)QuickKeys::SnipArea) {
            toast(QStringLiteral("Выделите область на ПК — снимок придёт в буфер ноутбука"));
        }
        else if (param == (int)QuickKeys::PrintScreen) {
            toast(QStringLiteral("Снимок экрана ПК — в буфере ноутбука"));
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
