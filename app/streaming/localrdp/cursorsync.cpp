#include "cursorsync.h"

#include <QSettings>

namespace LocalRdp {

CursorSync::CursorSync(Host* host, const QString& hostId)
    : m_Host(host),
      m_Key(QStringLiteral("localrdp/sunshinecursor/") + (hostId.isEmpty() ? QStringLiteral("default") : hostId))
{
    QSettings settings;
    // Earlier builds kept one record for all PCs: take it over once
    const QString key = settings.contains(m_Key + "/instance") ? m_Key : QStringLiteral("localrdp/sunshinecursor");
    m_RecordInstance = settings.value(key + "/instance").toString();
    m_RecordHidden = settings.value(key + "/hidden", false).toBool();
}

void CursorSync::update(bool enabled, bool absoluteMouse, bool agentConnected, const QString& sunshineInstance)
{
    if (!sunshineInstance.isEmpty()) {
        m_Instance = sunshineInstance;
    }
    // Starting needs the agent (it tells us which Sunshine run this is). Once
    // started, losing the agent does not stop it: that usually happens because
    // the whole connection is going down, and a key sent now could get lost and
    // leave our record of Sunshine's toggle wrong. Only the user's own changes
    // (setting, mouse mode) stop it, while the stream is certainly alive.
    const bool active = enabled && absoluteMouse && !m_Instance.isEmpty() && (agentConnected || m_Active);

    // Sunshine's cursor in the video: hidden exactly while we are active.
    // A Sunshine run we have no record of starts with it shown.
    if (!m_Instance.isEmpty()) {
        const bool hidden = m_RecordInstance == m_Instance && m_RecordHidden;
        if (hidden != active) {
            m_Host->toggleSunshineCursor();
            m_RecordInstance = m_Instance;
            m_RecordHidden = active;
            QSettings settings;
            settings.setValue(m_Key + "/instance", m_RecordInstance);
            settings.setValue(m_Key + "/hidden", m_RecordHidden);
        }
    }

    // The local cursor
    if (active && m_Active && m_AgentConnected && !agentConnected) {
        // The PC's cursor is unknown now: a plain arrow until the agent is back
        m_PcCursor = Proto::CursorState();
        m_Host->showPcCursor(m_PcCursor);
    }
    if (active != m_Active || !m_Started) {
        if (active) {
            m_Host->showPcCursor(m_PcCursor);
        }
        else if (m_Started) {
            m_Host->restoreLocalCursor();
        }
    }
    m_Active = active;
    m_AgentConnected = agentConnected;
    m_Started = true;
}

void CursorSync::fixDoubleCursor()
{
    if (m_Active) {
        m_Host->toggleSunshineCursor();
    }
}

void CursorSync::setPcCursor(const Proto::CursorState& cursor)
{
    m_PcCursor = cursor;
    if (m_Active) {
        m_Host->showPcCursor(cursor);
    }
}

}
