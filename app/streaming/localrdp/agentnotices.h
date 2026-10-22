#pragma once

#include "streamactions.h"

#include <QString>

namespace LocalRdp {

// Short notices for the user when the agent connection or the PC's lock state
// changes (shown under the toolbar). Quiet until the agent was connected once:
// without an agent the status dot already tells.
class AgentNotices
{
public:
    // The notice for this change, or an empty string.
    QString update(AgentStatus status, bool pcLocked);

private:
    bool m_EverConnected = false;
    bool m_Connected = false;
    bool m_Lost = false;
    bool m_Locked = false;
};

}
