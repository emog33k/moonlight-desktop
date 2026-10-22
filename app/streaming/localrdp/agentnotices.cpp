#include "agentnotices.h"

namespace LocalRdp {

QString AgentNotices::update(AgentStatus status, bool pcLocked)
{
    const bool connected = status == AgentStatus::Connected;
    QString notice;

    if (connected && !m_Connected) {
        if (m_Lost) {
            notice = QStringLiteral("Агент на ПК снова на связи");
        }
        m_Lost = false;
        // The lock state as we find it is not news
        m_Locked = pcLocked;
    }
    else if (!connected && m_Connected) {
        m_Lost = true;
        notice = QStringLiteral("Связь с агентом на ПК потеряна: буфер обмена и блокировка ПК не работают");
    }
    else if (connected && pcLocked != m_Locked) {
        m_Locked = pcLocked;
        notice = pcLocked ? QStringLiteral("ПК заблокирован") : QStringLiteral("ПК разблокирован");
    }

    m_EverConnected = m_EverConnected || connected;
    m_Connected = connected;
    return notice;
}

}
