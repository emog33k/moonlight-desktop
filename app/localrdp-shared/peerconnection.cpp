#include "peerconnection.h"

namespace LocalRdp {

PeerConnection::PeerConnection(QSslSocket* socket, QObject* parent)
    : QObject(parent),
      m_Socket(socket)
{
    static const int frameTypeId = qRegisterMetaType<LocalRdp::Proto::Frame>();
    (void)frameTypeId;

    m_Socket->setParent(this);
    connect(m_Socket, &QIODevice::readyRead, this, &PeerConnection::onReadyRead);
    connect(m_Socket, &QAbstractSocket::disconnected, this, &PeerConnection::onDisconnected);
    connect(m_Socket, &QAbstractSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        if (m_Socket->state() == QAbstractSocket::UnconnectedState) {
            onDisconnected();
        }
    });

    // Bytes may already be waiting (e.g. right after the handshake)
    if (m_Socket->bytesAvailable() > 0) {
        QMetaObject::invokeMethod(this, &PeerConnection::onReadyRead, Qt::QueuedConnection);
    }
}

PeerConnection::~PeerConnection()
{
    m_Closed = true; // no signals from a dying object
    m_Socket->disconnect(this);
    m_Socket->abort();
}

void PeerConnection::send(Proto::MsgType type, const QByteArray& payload)
{
    if (m_Closed) {
        return;
    }
    m_Socket->write(Proto::encodeFrame(type, payload));
}

void PeerConnection::close()
{
    if (m_Closed) {
        return;
    }
    m_Socket->flush();
    m_Socket->disconnectFromHost();
}

void PeerConnection::onReadyRead()
{
    if (m_Closed) {
        return;
    }
    m_Decoder.feed(m_Socket->readAll());
    while (auto frame = m_Decoder.next()) {
        emit frameReceived(*frame);
        if (m_Closed) {
            return;
        }
    }
    if (m_Decoder.failed()) {
        m_Socket->abort();
        onDisconnected();
    }
}

void PeerConnection::onDisconnected()
{
    if (m_Closed) {
        return;
    }
    m_Closed = true;
    emit closed();
}

QSslSocket* connectPinned(const QString& host, quint16 port,
                          const QSslCertificate& clientCert, const QSslKey& clientKey,
                          const QSslCertificate& pinnedServer)
{
    auto socket = new QSslSocket();
    QSslConfiguration config = QSslConfiguration::defaultConfiguration();
    config.setLocalCertificate(clientCert);
    config.setPrivateKey(clientKey);
    config.setProtocol(QSsl::TlsV1_2OrLater);
    config.setPeerVerifyMode(QSslSocket::VerifyPeer);
    socket->setSslConfiguration(config);

    // The server's certificate is self-signed, so verification always reports
    // errors. Accept them only when the presented certificate is the pinned one
    // (same rule Moonlight uses for Sunshine in NvHTTP::handleSslErrors).
    QObject::connect(socket, &QSslSocket::sslErrors, socket, [socket, pinnedServer](const QList<QSslError>& errors) {
        if (pinnedServer.isNull() || socket->peerCertificate() != pinnedServer) {
            return; // handshake fails and the socket disconnects
        }
        for (const QSslError& error : errors) {
            if (!error.certificate().isNull() && error.certificate() != pinnedServer) {
                return;
            }
        }
        socket->ignoreSslErrors(errors);
    });

    socket->connectToHostEncrypted(host, port);
    return socket;
}

}
