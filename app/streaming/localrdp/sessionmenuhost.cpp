#include "sessionmenuhost.h"
#include "agentclient.h"
#include "settings/streamingpreferences.h"

namespace LocalRdp {

SessionMenuHost::SessionMenuHost(StreamMenuHost* input, StreamingPreferences* prefs, AgentClient* agent)
    : m_Input(input),
      m_Prefs(prefs),
      m_Agent(agent)
{
}

StreamState SessionMenuHost::queryState()
{
    StreamState state = m_Input->queryState();
    state.agentStatus = m_Agent ? m_Agent->status() : AgentStatus::NotFound;
    state.lockOnDisconnect = m_Prefs->lockOnDisconnect;
    return state;
}

void SessionMenuHost::performAction(StreamAction action)
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
    default:
        m_Input->performAction(action);
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
