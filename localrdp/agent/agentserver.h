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
    // While the PC is locked nothing of its clipboard or files is shared, and
    // nothing from the laptop lands on it. Polled every lockPollMs.
    std::function<bool()> isSessionLocked;
    int lockPollMs = 1000;
    // Touches every monitor so the screen capture gets a new frame (Sunshine,
    // after switching to another monitor, waits for something to change).
    // Done at each of refreshDelaysMs after the laptop asks.
    std::function<void()> refreshScreens;
    // Restarts or shuts the PC down ("reboot" / "shutdown"); only these two words.
    std::function<void(const QString&)> powerAction;
    QVector<int> refreshDelaysMs { 300, 800, 1500 };
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
    void checkSessionLock();
    void sendPcState();

    AgentConfig m_Config;
    QSslServer* m_Server;
    QPointer<PeerConnection> m_Session;
    bool m_SessionSaidGoodbye = false;
    // The laptop said Hello and has not said Goodbye: any loss of the connection
    // from now on (also of a reconnect that never got to Hello) starts the countdown
    bool m_LaptopPresent = false;
    bool m_LockOnDisconnect = false;
    QTimer m_GraceTimer;
    QTimer m_IdleTimer;      // restarted by every frame of the session
    QTimer m_DisplayTimer;
    QByteArray m_SentDisplays; // last DisplayList payload sent to this session
    QByteArray m_SeenDisplays; // changed list seen once; sent when seen again (no flicker mid-change)
    QTimer m_LockTimer;
    bool m_PcLocked = false;
    qint64 m_LastRejectLog = 0;
    qint64 m_LastRefresh = 0;
    int m_RejectsNotLogged = 0;
    QTimer m_CursorTimer;
    Proto::CursorKind m_SentCursorKind = Proto::CursorKind::Default;
    quint64 m_SentCursorKey = 0;
    bool m_SessionReady = false; // Hello received
    std::unique_ptr<Clip::ClipboardBridge> m_Clipboard;
};

}
