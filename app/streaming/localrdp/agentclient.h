#pragma once

#include "streamactions.h"
#include "localrdp-shared/cursorshape.h"
#include "localrdp-shared/protocol.h"

#include <QSslCertificate>
#include <QSslKey>
#include <QString>
#include <QThread>

#include <QStringList>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>

namespace LocalRdp {

class AgentClientWorker;
namespace Clip { class ClipboardBridge; }

// When the laptop's clipboard was copied, as far as we can tell at stream start:
// unchanged since the last stream (same Windows clipboard sequence number) keeps
// the time we knew then, otherwise it counts as copied now.
qint64 laptopClipboardCopyTime(quint32 currentSeq, quint32 storedSeq, qint64 storedTime, qint64 now);

// Connection from the stream to LocalRDP Agent on the PC. Runs on its own thread
// (the Qt main thread is busy with the SDL loop during a stream) and keeps
// reconnecting while it exists. All methods are safe to call from any thread.
class AgentClient
{
public:
    AgentClient(const QString& host, const QSslCertificate& clientCert, const QSslKey& clientKey,
                const QSslCertificate& pinnedServer, quint16 port = Proto::kAgentPort,
                int retryMs = 3000, int pingMs = 2000, int pongTimeoutMs = 6000,
                bool useSystemClipboard = true);
    ~AgentClient(); // returns within about a second

    AgentClient(const AgentClient&) = delete;
    AgentClient& operator=(const AgentClient&) = delete;

    AgentStatus status() const { return (AgentStatus)m_Status.load(); }

    void lockNow();
    // After a monitor switch: the agent makes Sunshine send a fresh frame
    void refreshScreens();
    // "reboot" or "shutdown" the PC
    void powerAction(const QString& what);
    void setLockOnDisconnect(bool enabled);
    // Tell the agent the user is ending the stream on purpose (before destroying).
    void sayGoodbye();

    // Clipboard and file sync with the PC (on by default).
    void setClipboardSync(bool enabled);
    // Offer files to the PC as if copied on the laptop (paste there with Ctrl+V).
    // done(ok, reason) runs on a background thread with the outcome.
    void offerFiles(const QStringList& paths, std::function<void(bool, QString)> done = {});

    // The PC's monitors as Sunshine numbers them (empty until the agent reports them).
    QVector<DisplayInfo> displays() const;
    // The monitor Sunshine started the stream on (-1 if unknown).
    int initialDisplay() const;

    // Whether the PC's Windows session is locked (as the agent last said).
    bool pcLocked() const { return m_PcLocked; }

    // Identifies the running Sunshine (from the agent); empty until known.
    QString sunshineInstance() const;
    // The PC's latest cursor, if it changed since the last call.
    std::optional<Proto::CursorState> takeCursor();
    // Called on the agent thread whenever the status, Sunshine instance or PC
    // cursor changes. Keep it short (post an event).
    void setChangeHandler(std::function<void()> handler);

private:
    std::atomic<int> m_Status;
    std::unique_ptr<Clip::ClipboardBridge> m_Clipboard;
    QThread m_Thread;
    AgentClientWorker* m_Worker;
    // The clipboard bridge's threads send through this; cleared before the worker stops.
    std::mutex m_SendMutex;
    mutable std::mutex m_DisplaysMutex;
    QVector<DisplayInfo> m_Displays;
    int m_InitialDisplay = -1;
    mutable std::mutex m_StateMutex;
    QString m_SunshineInstance;
    std::optional<Proto::CursorState> m_Cursor;
    std::function<void()> m_OnChange;
    bool m_UseSystemClipboard = true;
    std::atomic<bool> m_PcLocked { false };
    void notifyChange();
    AgentClientWorker* m_SendTarget = nullptr;
};

}
