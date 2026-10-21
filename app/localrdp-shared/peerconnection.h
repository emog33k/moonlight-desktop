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

signals:
    void frameReceived(const LocalRdp::Proto::Frame& frame);
    void closed();

private:
    void onReadyRead();
    void onDisconnected();

    QSslSocket* m_Socket;
    Proto::FrameDecoder m_Decoder;
    bool m_Closed = false;
};

// Client side: starts a TLS connection that presents our client certificate and
// accepts the server only if its certificate is exactly `pinnedServer` (the
// certificate Moonlight stored when pairing with Sunshine). The caller owns the
// returned socket; wait for encrypted() before using it.
QSslSocket* connectPinned(const QString& host, quint16 port,
                          const QSslCertificate& clientCert, const QSslKey& clientKey,
                          const QSslCertificate& pinnedServer);

}
