#pragma once

#include "protocol.h"

#include <QObject>
#include <QSslCertificate>
#include <QSslKey>
#include <QSslConfiguration>
#include <QSslSocket>

Q_DECLARE_METATYPE(LocalRdp::Proto::Frame)

namespace LocalRdp {

// Frames over one TLS socket. Owns the socket. A malformed frame from the peer
// aborts the connection.
class PeerConnection : public QObject
{
    Q_OBJECT

public:
    explicit PeerConnection(QSslSocket* socket, QObject* parent = nullptr);
    ~PeerConnection() override;

    void send(Proto::MsgType type, const QByteArray& payload = QByteArray());
    void close();
    QSslSocket* socket() const { return m_Socket; }

    // More queued for sending than this means the peer stopped reading: drop it
    static constexpr qint64 kMaxQueuedBytes = 320ll * 1024 * 1024;

signals:
    void frameReceived(const LocalRdp::Proto::Frame& frame);
    // Bytes arrived (also in the middle of a large frame)
    void activity();
    void closed();

private:
    void onReadyRead();
    void onDisconnected();

    QSslSocket* m_Socket;
    Proto::FrameDecoder m_Decoder;
    bool m_Closed = false;
};

// Client side: starts a TLS connection that presents our client certificate and
// accepts the server only if its certificate is `pinnedServer` (the certificate
// Moonlight stored when pairing with Sunshine) or an agent certificate signed by
// it (see agentidentity.h). The caller owns the returned socket; wait for
// encrypted() before using it.
QSslSocket* connectPinned(const QString& host, quint16 port,
                          const QSslCertificate& clientCert, const QSslKey& clientKey,
                          const QSslCertificate& pinnedServer);

}
