#include "agentserver.h"
#include "localrdp-shared/trust.h"

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
    m_Server->setHandshakeTimeout(10000);

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

    m_GraceTimer.setSingleShot(true);
    connect(&m_GraceTimer, &QTimer::timeout, this, [this]() { lockNow("no reconnect within grace period"); });
}

AgentServer::~AgentServer()
{
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
        emit message("unpaired client rejected from " + socket->peerAddress().toString() + " (" +
                     (peer.isNull() ? QString("no certificate")
                                    : "certificate " + peer.subjectDisplayName() + ", sha256 " +
                                      QString::fromLatin1(peer.digest(QCryptographicHash::Sha256).toHex().left(16))) +
                     (state.isEmpty() ? QString(", Sunshine state file unreadable") : QString()) + ")");
        socket->abort();
        socket->deleteLater();
        return;
    }

    // The laptop is back: forget a pending lock from an earlier drop
    m_GraceTimer.stop();

    if (m_Session) {
        // Replaced by the new connection, not a disconnect: no lock
        PeerConnection* old = m_Session;
        m_Session = nullptr;
        old->disconnect(this);
        old->deleteLater();
        emit message("previous session replaced");
    }

    socket->setParent(nullptr);
    auto conn = new PeerConnection(socket, this);
    m_Session = conn;
    m_SessionSaidGoodbye = false;
    m_LockOnDisconnect = false;
    connect(conn, &PeerConnection::frameReceived, this, [this, conn](const Frame& f) { onFrame(conn, f); });
    connect(conn, &PeerConnection::closed, this, [this, conn]() { onSessionClosed(conn); });
    emit message("paired client connected from " + socket->peerAddress().toString());
}

void AgentServer::onFrame(PeerConnection* conn, const Frame& frame)
{
    switch (frame.type) {
    case MsgType::Hello:
        conn->send(MsgType::HelloAck, jsonPayload(QJsonObject{
            { "version", (int)kProtocolVersion },
            { "agent", "LocalRDP Agent" },
        }));
        break;
    case MsgType::Ping:
        conn->send(MsgType::Pong, frame.payload);
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
    m_Session = nullptr;
    conn->deleteLater();
    emit message("session closed");

    if (!m_SessionSaidGoodbye && m_LockOnDisconnect) {
        m_GraceTimer.start(m_Config.reconnectGraceMs);
    }
}

void AgentServer::lockNow(const char* reason)
{
    emit message(QString("locking the PC: ") + reason);
    if (m_Config.lockWorkstation) {
        m_Config.lockWorkstation();
    }
}

}
