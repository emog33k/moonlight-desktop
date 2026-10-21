#pragma once

#include "localrdp-shared/clip/clipboardbridge_win32.h"
#include "localrdp-shared/cursorshape.h"
#include "localrdp-shared/hostdisplays.h"
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
    // The PC's monitors in Sunshine's order; sent after Hello and again whenever
    // they change (checked every displayPollMs). Not set: never sent.
    std::function<Proto::DisplayList()> listDisplays;
    int displayPollMs = 3000;
    // The PC's cursor, polled every cursorPollMs while a laptop is connected and
    // sent when it changes. Not set: never sent.
    std::function<Proto::CursorState()> readCursor;
    int cursorPollMs = 33;
    // Identifies the running Sunshine (its process id); empty if not running.
    // Reported in HelloAck so the laptop knows when Sunshine restarted.
    std::function<QString()> sunshineInstance;
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
    void sendDisplays(bool onlyIfChanged);
    void sendCursor(bool onlyIfChanged);

    AgentConfig m_Config;
    QSslServer* m_Server;
    QPointer<PeerConnection> m_Session;
    bool m_SessionSaidGoodbye = false;
    bool m_LockOnDisconnect = false;
    QTimer m_GraceTimer;
    QTimer m_IdleTimer;      // restarted by every frame of the session
    QTimer m_DisplayTimer;
    QByteArray m_SentDisplays; // last DisplayList payload sent to this session
    QTimer m_CursorTimer;
    Proto::CursorKind m_SentCursorKind = Proto::CursorKind::Default;
    quint64 m_SentCursorKey = 0;
    bool m_SessionReady = false; // Hello received
    std::unique_ptr<Clip::ClipboardBridge> m_Clipboard;
};

}
