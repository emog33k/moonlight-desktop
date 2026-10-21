#include "sessionmenuhost.h"
#include "agentclient.h"
#include "settings/streamingpreferences.h"

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
    state.currentDisplay = m_CurrentDisplay;
    if (m_Agent) {
        state.displays = m_Agent->displays();
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
