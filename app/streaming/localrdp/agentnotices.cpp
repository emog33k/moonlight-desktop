#include "agentnotices.h"
#include "localrdp-shared/desktoptr.h"

namespace LocalRdp {

QString AgentNotices::update(AgentStatus status, bool pcLocked)
{
    const bool connected = status == AgentStatus::Connected;
    QString notice;

    if (connected && !m_Connected) {
        if (m_Lost) {
            notice = dtr("The host agent is back");
        }
        m_Lost = false;
        // The lock state as we find it is not news
        m_Locked = pcLocked;
    }
    else if (!connected && m_Connected) {
        m_Lost = true;
        notice = dtr("Lost the host agent: clipboard and host lock do not work");
    }
    else if (connected && pcLocked != m_Locked) {
        m_Locked = pcLocked;
        notice = pcLocked ? dtr("Host locked") : dtr("Host unlocked");
    }

    m_EverConnected = m_EverConnected || connected;
    m_Connected = connected;
    return notice;
}

}
