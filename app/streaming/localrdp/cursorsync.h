#pragma once

#include "localrdp-shared/cursorshape.h"

#include <QString>

// The AnyDesk-style cursor: the laptop shows the PC's cursor shape as its own
// cursor (no lag) and Sunshine leaves its cursor out of the video.
//
// Sunshine only has a toggle for its cursor (Ctrl+Alt+Shift+N) that stays in
// effect until Sunshine restarts. We remember, per PC, which Sunshine run (its
// process id, reported by the agent) we hid it in, so we always know which way
// the toggle goes, also across streams.
//
// Starts in desktop mouse mode once the agent is connected; otherwise the
// cursor is drawn in the video as before.

namespace LocalRdp {

class CursorSync
{
public:
    class Host
    {
    public:
        virtual ~Host() = default;
        // Ctrl+Alt+Shift+N to Sunshine
        virtual void toggleSunshineCursor() = 0;
        // Show this as the local cursor over the video (hidden, arrow or shape)
        virtual void showPcCursor(const Proto::CursorState& cursor) = 0;
        // Back to Moonlight's own local cursor behaviour
        virtual void restoreLocalCursor() = 0;
    };

    // hostId: which PC (Moonlight's id for it); the record is kept per PC.
    explicit CursorSync(Host* host, const QString& hostId = QString());

    // enabled: the "Курсор как в AnyDesk" setting. sunshineInstance: from the
    // agent, empty if unknown (then nothing is toggled).
    void update(bool enabled, bool absoluteMouse, bool agentConnected, const QString& sunshineInstance);
    void setPcCursor(const Proto::CursorState& cursor);

    bool isActive() const { return m_Active; }

private:
    Host* m_Host;
    QString m_Key;            // settings group for this PC
    QString m_RecordInstance; // record: Sunshine run in which...
    bool m_RecordHidden = false; // ...its cursor was hidden by us
    bool m_Active = false;
    bool m_AgentConnected = false;
    bool m_Started = false;
    QString m_Instance; // last Sunshine run the agent told us about
    Proto::CursorState m_PcCursor;
};

}
