#pragma once

#include "streamactions.h"
#include "localrdp-shared/protocol.h"

#include <QSslCertificate>
#include <QSslKey>
#include <QString>
#include <QThread>

#include <QStringList>

#include <atomic>
#include <memory>
#include <mutex>

namespace LocalRdp {

class AgentClientWorker;
namespace Clip { class ClipboardBridge; }

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
    void setLockOnDisconnect(bool enabled);
    // Tell the agent the user is ending the stream on purpose (before destroying).
    void sayGoodbye();

    // Clipboard and file sync with the PC (on by default).
    void setClipboardSync(bool enabled);
    // Offer files to the PC as if copied on the laptop (paste there with Ctrl+V).
    void offerFiles(const QStringList& paths);

private:
    std::atomic<int> m_Status;
    std::unique_ptr<Clip::ClipboardBridge> m_Clipboard;
    QThread m_Thread;
    AgentClientWorker* m_Worker;
    // The clipboard bridge's threads send through this; cleared before the worker stops.
    std::mutex m_SendMutex;
    AgentClientWorker* m_SendTarget = nullptr;
};

}
