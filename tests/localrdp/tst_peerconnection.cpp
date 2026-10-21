#include "tests.h"
#include "localrdp-shared/peerconnection.h"

#include <QFile>
#include <QSignalSpy>
#include <QSslKey>
#include <QSslServer>
#include <QTest>

using namespace LocalRdp;
using namespace LocalRdp::Proto;

QSslCertificate testCert(const QString& name);

QSslKey testKey(const QString& name)
{
    QFile f(":/testdata/" + name + ".key");
    f.open(QIODevice::ReadOnly);
    return QSslKey(f.readAll(), QSsl::Rsa, QSsl::Pem);
}

// A TLS server presenting `certName`, answering Ping with Pong (or garbage when asked).
static QSslServer* makeEchoServer(QObject* parent, const QString& certName, bool sendGarbage = false)
{
    auto server = new QSslServer(parent);
    QSslConfiguration config = QSslConfiguration::defaultConfiguration();
    config.setLocalCertificate(testCert(certName));
    config.setPrivateKey(testKey(certName));
    config.setPeerVerifyMode(QSslSocket::VerifyPeer);
    server->setSslConfiguration(config);
    QObject::connect(server, &QSslServer::sslErrors, server, [](QSslSocket* s, const QList<QSslError>& e) {
        s->ignoreSslErrors(e); // test server: accept any client cert
    });
    QObject::connect(server, &QTcpServer::pendingConnectionAvailable, server, [server, sendGarbage]() {
        auto socket = qobject_cast<QSslSocket*>(server->nextPendingConnection());
        if (sendGarbage) {
            socket->write(QByteArray::fromHex("7fffffff00")); // absurd length
            return;
        }
        auto conn = new PeerConnection(socket, server);
        QObject::connect(conn, &PeerConnection::frameReceived, conn, [conn](const Frame& f) {
            if (f.type == MsgType::Ping) {
                conn->send(MsgType::Pong, f.payload);
            }
        });
    });
    server->listen(QHostAddress::LocalHost, 0);
    return server;
}

void TestPeerConnection::pinnedServerConnects()
{
    QObject scope;
    QSslServer* server = makeEchoServer(&scope, "server");
    QSslSocket* socket = connectPinned("127.0.0.1", server->serverPort(),
                                       testCert("client1"), testKey("client1"), testCert("server"));
    QSignalSpy encrypted(socket, &QSslSocket::encrypted);
    QVERIFY(encrypted.wait(5000));

    PeerConnection conn(socket);
    QSignalSpy frames(&conn, &PeerConnection::frameReceived);
    conn.send(MsgType::Ping, "hi");
    QVERIFY(frames.wait(5000));
    const Frame f = frames.at(0).at(0).value<Frame>();
    QCOMPARE(f.type, MsgType::Pong);
    QCOMPARE(f.payload, QByteArray("hi"));
}

void TestPeerConnection::wrongServerCertRejected()
{
    QObject scope;
    QSslServer* server = makeEchoServer(&scope, "client2"); // not the pinned cert
    QSslSocket* socket = connectPinned("127.0.0.1", server->serverPort(),
                                       testCert("client1"), testKey("client1"), testCert("server"));
    QSignalSpy encrypted(socket, &QSslSocket::encrypted);
    QSignalSpy disconnected(socket, &QAbstractSocket::disconnected);
    QVERIFY(disconnected.wait(5000) || socket->state() == QAbstractSocket::UnconnectedState);
    QCOMPARE(encrypted.count(), 0);
    delete socket;
}

void TestPeerConnection::garbageFrameCloses()
{
    QObject scope;
    QSslServer* server = makeEchoServer(&scope, "server", true);
    QSslSocket* socket = connectPinned("127.0.0.1", server->serverPort(),
                                       testCert("client1"), testKey("client1"), testCert("server"));
    PeerConnection conn(socket);
    QSignalSpy closed(&conn, &PeerConnection::closed);
    QVERIFY(closed.wait(5000));
}
