#include "agentserver.h"
#include "localrdp-shared/trust.h"

#include <QDateTime>
#include <QJsonObject>
#include <QSslServer>

namespace LocalRdp {

using namespace Proto;

AgentServer::AgentServer(AgentConfig config, QObject* parent)
    : QObject(parent),
      m_Config(std::move(config)),
      m_Server(new QSslServer(this))
{
    QSslConfiguration ssl = QSslConfiguration::defaultConfiguration();
    ssl.setLocalCertificate(m_Config.serverCert);
    ssl.setPrivateKey(m_Config.serverKey);
    ssl.setProtocol(QSsl::TlsV1_2OrLater);
    // Ask for the client's certificate. It is self-signed (Moonlight makes its own),
    // so chain verification always fails; we accept the handshake and then check the
    // certificate against Sunshine's paired devices before reading anything.
    ssl.setPeerVerifyMode(QSslSocket::VerifyPeer);
    m_Server->setSslConfiguration(ssl);
    // Short handshake and a long queue: idle connections from the LAN cannot
    // keep the laptop out for long
    m_Server->setHandshakeTimeout(4000);
    m_Server->setMaxPendingConnections(256);

    connect(m_Server, &QSslServer::sslErrors, this, [](QSslSocket* socket, const QList<QSslError>& errors) {
        socket->ignoreSslErrors(errors);
    });
    connect(m_Server, &QSslServer::startedEncryptionHandshake, this, [this](QSslSocket* socket) {
        connect(socket, &QSslSocket::encrypted, this, [this, socket]() { onEncrypted(socket); });
    });
    connect(m_Server, &QTcpServer::pendingConnectionAvailable, this, [this]() {
        // Sockets are taken in onEncrypted(); drain the queue so it never fills up.
        while (m_Server->hasPendingConnections()) {
            m_Server->nextPendingConnection();
        }
    });

    // Clipboard sync with the laptop. Its threads hand frames back to ours.
    m_Clipboard.reset(new Clip::ClipboardBridge([this](MsgType type, QByteArray payload) {
        QMetaObject::invokeMethod(this, [this, type, payload]() {
            if (m_Session && m_SessionReady) {
                m_Session->send(type, payload);
            }
        }, Qt::QueuedConnection);
    }, m_Config.useSystemClipboard));
    // Same copy time on both sides (rare): the laptop's clipboard wins
    m_Clipboard->setPreferPeerOnTie(true);
    if (m_Config.onPeerOffer) {
        m_Clipboard->setPeerOfferObserver(m_Config.onPeerOffer);
    }

    m_GraceTimer.setSingleShot(true);
    connect(&m_GraceTimer, &QTimer::timeout, this, [this]() { lockNow("no reconnect within grace period"); });

    connect(&m_DisplayTimer, &QTimer::timeout, this, [this]() { sendDisplays(true); });
    connect(&m_CursorTimer, &QTimer::timeout, this, [this]() { sendCursor(true); });

    if (m_Config.isSessionLocked) {
        connect(&m_LockTimer, &QTimer::timeout, this, &AgentServer::checkSessionLock);
        m_LockTimer.start(m_Config.lockPollMs);
        checkSessionLock();
    }

    // A laptop that vanished (Wi-Fi gone, lid closed) never sends a FIN; its
    // pings stop instead. Treat that like a dropped connection.
    m_IdleTimer.setSingleShot(true);
    connect(&m_IdleTimer, &QTimer::timeout, this, [this]() {
        if (m_Session) {
            emit message("no traffic from the laptop; dropping the session");
            m_Session->socket()->abort();
        }
    });
}

AgentServer::~AgentServer()
{
    m_Clipboard.reset(); // stops its threads before we go away
    m_GraceTimer.stop();
    delete m_Session;
}

bool AgentServer::listen()
{
    if (!m_Server->listen(m_Config.bindAddress, m_Config.port)) {
        emit message("listen failed: " + m_Server->errorString());
        return false;
    }
    emit message(QString("listening on port %1").arg(m_Server->serverPort()));
    return true;
}

quint16 AgentServer::port() const
{
    return m_Server->serverPort();
}

void AgentServer::onEncrypted(QSslSocket* socket)
{
    const QSslCertificate peer = socket->peerCertificate();
    const QByteArray state = m_Config.readSunshineState ? m_Config.readSunshineState() : QByteArray();
    if (!isPairedClient(state, peer)) {
        // At most one line per 10 s, so a flood cannot push real entries out of the log
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (now - m_LastRejectLog >= 10000) {
            emit message("unpaired client rejected from " + socket->peerAddress().toString() + " (" +
                         (peer.isNull() ? QString("no certificate")
                                        : "certificate " + peer.subjectDisplayName().left(64) + ", sha256 " +
                                          QString::fromLatin1(peer.digest(QCryptographicHash::Sha256).toHex().left(16))) +
                         (state.isEmpty() ? QString(", Sunshine state file unreadable") : QString()) +
                         (m_RejectsNotLogged > 0 ? QString(", %1 more since the last line").arg(m_RejectsNotLogged) : QString()) +
                         ")");
            m_LastRejectLog = now;
            m_RejectsNotLogged = 0;
        }
        else {
            m_RejectsNotLogged++;
        }
        socket->abort();
        socket->deleteLater();
        return;
    }

    if (m_Session) {
        // Replaced by the new connection, not a disconnect: no lock
        PeerConnection* old = m_Session;
        m_Session = nullptr;
        m_SessionReady = false;
        m_DisplayTimer.stop();
        m_CursorTimer.stop();
        m_Clipboard->connectionDown();
        old->disconnect(this);
        old->deleteLater();
        emit message("previous session replaced");
    }

    socket->setParent(nullptr);
    auto conn = new PeerConnection(socket, this);
    m_Session = conn;
    m_SessionSaidGoodbye = false;
    // m_LockOnDisconnect keeps its last known value until the laptop says otherwise,
    // and a pending lock is only cancelled once the laptop has said Hello: a
    // reconnect that dies again right away must not leave the PC unlocked.
    m_IdleTimer.start(m_Config.idleTimeoutMs);
    connect(conn, &PeerConnection::frameReceived, this, [this, conn](const Frame& f) { onFrame(conn, f); });
    // Any bytes count as a sign of life (a big clipboard chunk on slow Wi-Fi takes a while)
    connect(conn, &PeerConnection::activity, this, [this, conn]() {
        if (conn == m_Session) {
            m_IdleTimer.start(m_Config.idleTimeoutMs);
        }
    });
    connect(conn, &PeerConnection::closed, this, [this, conn]() { onSessionClosed(conn); });
    emit message("paired client connected from " + socket->peerAddress().toString());
}

void AgentServer::onFrame(PeerConnection* conn, const Frame& frame)
{
    if (conn == m_Session) {
        m_IdleTimer.start(m_Config.idleTimeoutMs);
    }

    switch (frame.type) {
    case MsgType::Hello:
        // The laptop is really back: forget a pending lock from an earlier drop
        m_GraceTimer.stop();
        if (conn == m_Session && !m_SessionReady) {
            m_SessionReady = true;
            m_Clipboard->connectionUp();
        }
        if (auto o = parseJsonPayload(frame.payload)) {
            if (o->contains("lockOnDisconnect")) {
                m_LockOnDisconnect = o->value("lockOnDisconnect").toBool();
            }
        }
        conn->send(MsgType::HelloAck, jsonPayload(QJsonObject{
            { "version", (int)kProtocolVersion },
            { "agent", "LocalRDP Agent" },
            { "sunshine", m_Config.sunshineInstance ? m_Config.sunshineInstance() : QString() },
        }));
        if (conn == m_Session) {
            sendDisplays(false);
            if (m_Config.listDisplays) {
                m_DisplayTimer.start(m_Config.displayPollMs);
            }
            sendCursor(false);
            if (m_Config.readCursor) {
                m_CursorTimer.start(m_Config.cursorPollMs);
            }
        }
        break;
    case MsgType::Ping:
        conn->send(MsgType::Pong); // never echo what the peer sent
        break;
    case MsgType::LockNow:
        lockNow("requested from the laptop");
        break;
    case MsgType::SetLockOnDisconnect:
        if (auto o = parseJsonPayload(frame.payload)) {
            m_LockOnDisconnect = o->value("enabled").toBool();
        }
        break;
    case MsgType::Goodbye:
        m_SessionSaidGoodbye = true;
        if (m_LockOnDisconnect) {
            lockNow("laptop disconnected");
        }
        conn->close();
        break;
    case MsgType::ClipOffer:
    case MsgType::ClipRequest:
    case MsgType::ClipData:
    case MsgType::ClipError:
        if (conn == m_Session && m_SessionReady) {
            m_Clipboard->onFrame(frame);
        }
        break;
    default:
        // Unknown or not yet supported: ignore (newer client)
        break;
    }
}

void AgentServer::onSessionClosed(PeerConnection* conn)
{
    if (conn != m_Session) {
        return;
    }
    const bool saidHello = m_SessionReady;
    m_Session = nullptr;
    m_IdleTimer.stop();
    m_DisplayTimer.stop();
    m_CursorTimer.stop();
    m_SessionReady = false;
    m_Clipboard->connectionDown();
    conn->deleteLater();
    emit message("session closed");

    // Only a laptop that was really here (said Hello) starts the countdown; a
    // connection that died before that leaves a running countdown as it is.
    if (saidHello && !m_SessionSaidGoodbye && m_LockOnDisconnect) {
        m_GraceTimer.start(m_Config.reconnectGraceMs);
    }
}

void AgentServer::sendDisplays(bool onlyIfChanged)
{
    if (!m_Config.listDisplays || !m_Session || !m_SessionReady) {
        return;
    }
    const DisplayList list = m_Config.listDisplays();
    const QByteArray payload = encodeDisplayList(list);
    if (onlyIfChanged) {
        // Monitors being switched (or the screen turned off) can briefly list
        // nothing or only some: send a change only once it is seen twice in a row.
        if (payload == m_SentDisplays || list.displays.isEmpty()) {
            m_SeenDisplays.clear();
            return;
        }
        if (payload != m_SeenDisplays) {
            m_SeenDisplays = payload;
            return;
        }
    }
    m_SeenDisplays.clear();
    m_SentDisplays = payload;
    m_Session->send(MsgType::DisplayList, payload);
}

void AgentServer::sendCursor(bool onlyIfChanged)
{
    if (!m_Config.readCursor || !m_Session || !m_SessionReady) {
        return;
    }
    const CursorState cursor = m_Config.readCursor();
    if (onlyIfChanged && cursor.kind == m_SentCursorKind &&
            (cursor.kind != CursorKind::Shape || cursor.key == m_SentCursorKey)) {
        return;
    }
    m_SentCursorKind = cursor.kind;
    m_SentCursorKey = cursor.key;
    switch (cursor.kind) {
    case CursorKind::Shape:
        m_Session->send(MsgType::CursorShape, encodeCursorImage(cursor.image));
        break;
    case CursorKind::Hidden:
        m_Session->send(MsgType::CursorHidden);
        break;
    case CursorKind::Default:
        m_Session->send(MsgType::CursorDefault);
        break;
    }
}

void AgentServer::checkSessionLock()
{
    const bool locked = m_Config.isSessionLocked();
    if (locked == m_PcLocked) {
        return;
    }
    m_PcLocked = locked;
    emit message(locked ? "PC locked: clipboard sharing paused" : "PC unlocked: clipboard sharing resumed");
    m_Clipboard->setEnabled(!locked);
}

void AgentServer::lockNow(const char* reason)
{
    emit message(QString("locking the PC: ") + reason);
    if (m_Config.lockWorkstation) {
        m_Config.lockWorkstation();
    }
}

}
