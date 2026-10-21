#pragma once

#include "localrdp-shared/clip/clipboardbridge_win32.h"
#include "localrdp-shared/peerconnection.h"
#include "localrdp-shared/protocol.h"

#include <QHostAddress>
#include <QObject>
#include <QPointer>
#include <QSslCertificate>
#include <QSslKey>
#include <QTimer>

#include <functional>
#include <memory>

class QSslServer;
class QSslSocket;

namespace LocalRdp {

struct AgentConfig {
    QSslCertificate serverCert;   // Sunshine's own certificate (pinned by Moonlight)
    QSslKey serverKey;
    std::function<QByteArray()> readSunshineState; // contents of sunshine_state.json
    std::function<void()> lockWorkstation;          // LockWorkStation() in the real agent
    int reconnectGraceMs = 15000;
    int idleTimeoutMs = 8000;   // no frame for this long = connection is dead (laptop pings every 2 s)
    quint16 port = Proto::kAgentPort;
    QHostAddress bindAddress = QHostAddress::Any;
    bool useSystemClipboard = true;                  // false in tests
    std::function<void(const Clip::Offer&)> onPeerOffer; // log/tests
};

// TLS server for the paired laptop. Only a client whose certificate is paired and
// enabled in Sunshine gets past the handshake; nothing it sends is read before that.
// One session at a time: a new paired connection replaces the old one.
class AgentServer : public QObject
{
    Q_OBJECT

public:
    explicit AgentServer(AgentConfig config, QObject* parent = nullptr);
    ~AgentServer() override;

    bool listen();
    quint16 port() const;

signals:
    // For the log
    void message(const QString& text);

private:
    void onEncrypted(QSslSocket* socket);
    void onFrame(PeerConnection* conn, const Proto::Frame& frame);
    void onSessionClosed(PeerConnection* conn);
    void lockNow(const char* reason);

    AgentConfig m_Config;
    QSslServer* m_Server;
    QPointer<PeerConnection> m_Session;
    bool m_SessionSaidGoodbye = false;
    bool m_LockOnDisconnect = false;
    QTimer m_GraceTimer;
    QTimer m_IdleTimer;      // restarted by every frame of the session
    bool m_SessionReady = false; // Hello received
    std::unique_ptr<Clip::ClipboardBridge> m_Clipboard;
};

}
