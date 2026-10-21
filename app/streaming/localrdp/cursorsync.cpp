#include "cursorsync.h"

#include <QSettings>

namespace LocalRdp {

static const char* const kInstanceKey = "localrdp/sunshinecursor/instance";
static const char* const kHiddenKey = "localrdp/sunshinecursor/hidden";

CursorSync::CursorSync(Host* host)
    : m_Host(host)
{
}

void CursorSync::update(bool enabled, bool absoluteMouse, bool agentConnected, const QString& sunshineInstance)
{
    if (!sunshineInstance.isEmpty()) {
        m_Instance = sunshineInstance;
    }
    const bool active = enabled && absoluteMouse && agentConnected && !sunshineInstance.isEmpty();

    // Sunshine's cursor in the video: hidden exactly while we are active.
    // A Sunshine run we have no record of starts with it shown.
    if (!m_Instance.isEmpty()) {
        QSettings settings;
        const bool hidden = settings.value(kInstanceKey).toString() == m_Instance &&
                            settings.value(kHiddenKey, false).toBool();
        if (hidden != active) {
            m_Host->toggleSunshineCursor();
            settings.setValue(kInstanceKey, m_Instance);
            settings.setValue(kHiddenKey, active);
        }
    }

    // The local cursor
    if (active != m_Active || !m_Started) {
        if (active) {
            m_Host->showPcCursor(m_PcCursor);
        }
        else if (m_Started) {
            m_Host->restoreLocalCursor();
        }
    }
    m_Active = active;
    m_Started = true;
}

void CursorSync::setPcCursor(const Proto::CursorState& cursor)
{
    m_PcCursor = cursor;
    if (m_Active) {
        m_Host->showPcCursor(cursor);
    }
}

}
