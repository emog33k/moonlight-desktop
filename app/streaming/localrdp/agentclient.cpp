#include "agentclient.h"
#include "localrdp-shared/peerconnection.h"

#include <QJsonObject>
#include <QPointer>
#include <QTimer>

namespace LocalRdp {

using namespace Proto;

// Lives on AgentClient's thread; owns the socket.
class AgentClientWorker : public QObject
{
public:
    AgentClientWorker(std::atomic<int>& status, QString host, QSslCertificate clientCert, QSslKey clientKey,
                      QSslCertificate pinnedServer, quint16 port, int retryMs)
        : m_Status(status),
          m_Host(std::move(host)),
          m_ClientCert(std::move(clientCert)),
          m_ClientKey(std::move(clientKey)),
          m_PinnedServer(std::move(pinnedServer)),
          m_Port(port),
          m_RetryMs(retryMs)
    {
    }

    void start()
    {
        m_RetryTimer = new QTimer(this);
        m_RetryTimer->setSingleShot(true);
        connect(m_RetryTimer, &QTimer::timeout, this, &AgentClientWorker::connectOnce);
        connectOnce();
    }

    void stop()
    {
        m_Stopping = true;
        if (m_RetryTimer) {
            m_RetryTimer->stop();
        }
        dropConnection();
    }

    void lockNow()
    {
        if (m_Conn && m_Ready) {
            m_Conn->send(MsgType::LockNow);
        }
    }

    void setLockOnDisconnect(bool enabled)
    {
        m_LockOnDisconnect = enabled;
        if (m_Conn && m_Ready) {
            sendLockOnDisconnect();
        }
    }

    void sayGoodbye()
    {
        if (m_Conn && m_Ready) {
            m_Conn->send(MsgType::Goodbye);
            m_Conn->socket()->flush();
            m_Conn->socket()->waitForBytesWritten(300);
        }
        m_Stopping = true;
    }

private:
    void setStatus(AgentStatus status) { m_Status.store((int)status); }

    void connectOnce()
    {
        if (m_Stopping) {
            return;
        }
        dropConnection();
        if (m_Status.load() != (int)AgentStatus::Connected) {
            // Keep showing the last problem while retrying, unless we never tried
            if (m_Status.load() == (int)AgentStatus::Unknown) {
                setStatus(AgentStatus::Connecting);
            }
        }

        m_Ready = false;
        m_Encrypted = false;
        m_Socket = connectPinned(m_Host, m_Port, m_ClientCert, m_ClientKey, m_PinnedServer);
        m_Socket->setParent(this);

        connect(m_Socket, &QSslSocket::encrypted, this, [this]() {
            m_Encrypted = true;
            QSslSocket* socket = m_Socket;
            m_Socket = nullptr;
            socket->disconnect(this);
            m_Conn = new PeerConnection(socket, this);
            connect(m_Conn, &PeerConnection::frameReceived, this, &AgentClientWorker::onFrame);
            connect(m_Conn, &PeerConnection::closed, this, &AgentClientWorker::onClosed);
            m_Conn->send(MsgType::Hello, jsonPayload(QJsonObject{
                { "version", (int)kProtocolVersion },
                { "client", "Moonlight LocalRDP" },
            }));
        });
        connect(m_Socket, &QAbstractSocket::errorOccurred, this, [this](QAbstractSocket::SocketError error) {
            if (m_Encrypted) {
                return;
            }
            // The handshake failing means the PC did not show the certificate we
            // paired with: someone else is answering on the agent port.
            setStatus(error == QAbstractSocket::SslHandshakeFailedError ? AgentStatus::CertError
                                                                        : AgentStatus::NotFound);
            scheduleRetry();
        });
    }

    void onFrame(const Frame& frame)
    {
        if (frame.type == MsgType::HelloAck) {
            m_Ready = true;
            setStatus(AgentStatus::Connected);
            sendLockOnDisconnect();
        }
    }

    void onClosed()
    {
        // Closed right after TLS without a HelloAck: the agent refused our certificate
        // (laptop not paired with Sunshine, or unpaired since).
        setStatus(m_Ready ? AgentStatus::Connecting : AgentStatus::Rejected);
        m_Ready = false;
        if (m_Conn) {
            m_Conn->deleteLater();
            m_Conn = nullptr;
        }
        scheduleRetry();
    }

    void sendLockOnDisconnect()
    {
        m_Conn->send(MsgType::SetLockOnDisconnect, jsonPayload(QJsonObject{ { "enabled", m_LockOnDisconnect } }));
    }

    void scheduleRetry()
    {
        if (!m_Stopping && m_RetryTimer && !m_RetryTimer->isActive()) {
            m_RetryTimer->start(m_RetryMs);
        }
    }

    void dropConnection()
    {
        if (m_Conn) {
            m_Conn->disconnect(this);
            delete m_Conn;
            m_Conn = nullptr;
        }
        if (m_Socket) {
            m_Socket->disconnect(this);
            m_Socket->abort();
            delete m_Socket;
            m_Socket = nullptr;
        }
        m_Ready = false;
    }

    std::atomic<int>& m_Status;
    QString m_Host;
    QSslCertificate m_ClientCert;
    QSslKey m_ClientKey;
    QSslCertificate m_PinnedServer;
    quint16 m_Port;
    int m_RetryMs;

    QTimer* m_RetryTimer = nullptr;
    QSslSocket* m_Socket = nullptr;  // until the handshake completes
    PeerConnection* m_Conn = nullptr; // after it
    bool m_Encrypted = false;
    bool m_Ready = false;             // HelloAck received
    bool m_LockOnDisconnect = false;
    bool m_Stopping = false;
};

AgentClient::AgentClient(const QString& host, const QSslCertificate& clientCert, const QSslKey& clientKey,
                         const QSslCertificate& pinnedServer, quint16 port, int retryMs)
    : m_Status((int)AgentStatus::Unknown)
{
    m_Worker = new AgentClientWorker(m_Status, host, clientCert, clientKey, pinnedServer, port, retryMs);
    m_Worker->moveToThread(&m_Thread);
    QObject::connect(&m_Thread, &QThread::finished, m_Worker, &QObject::deleteLater);
    m_Thread.setObjectName("LocalRDP agent");
    m_Thread.start();
    QMetaObject::invokeMethod(m_Worker, [w = m_Worker]() { w->start(); }, Qt::QueuedConnection);
}

AgentClient::~AgentClient()
{
    QMetaObject::invokeMethod(m_Worker, [w = m_Worker]() { w->stop(); }, Qt::BlockingQueuedConnection);
    m_Thread.quit();
    m_Thread.wait(1000);
}

void AgentClient::lockNow()
{
    QMetaObject::invokeMethod(m_Worker, [w = m_Worker]() { w->lockNow(); }, Qt::QueuedConnection);
}

void AgentClient::setLockOnDisconnect(bool enabled)
{
    QMetaObject::invokeMethod(m_Worker, [w = m_Worker, enabled]() { w->setLockOnDisconnect(enabled); },
                              Qt::QueuedConnection);
}

void AgentClient::sayGoodbye()
{
    QMetaObject::invokeMethod(m_Worker, [w = m_Worker]() { w->sayGoodbye(); }, Qt::BlockingQueuedConnection);
}

}
