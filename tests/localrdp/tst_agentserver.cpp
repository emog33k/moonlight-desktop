#include "tests.h"
#include "agentserver.h"
#include "localrdp-shared/peerconnection.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTest>

#include <memory>

using namespace LocalRdp;
using namespace LocalRdp::Proto;

QSslCertificate testCert(const QString& name);
QSslKey testKey(const QString& name);
QByteArray sunshineState(const QList<QPair<QSslCertificate, QString>>& devices);

namespace {

struct Fixture {
    QByteArray state;
    int locks = 0;
    std::unique_ptr<AgentServer> server;

    explicit Fixture(int idleTimeoutMs = 60000)
    {
        state = sunshineState({ { testCert("client1"), "true" } });
        AgentConfig config;
        config.serverCert = testCert("server");
        config.serverKey = testKey("server");
        config.readSunshineState = [this]() { return state; };
        config.lockWorkstation = [this]() { locks++; };
        config.reconnectGraceMs = 300;
        config.idleTimeoutMs = idleTimeoutMs;
        config.port = 0;
        config.bindAddress = QHostAddress::LocalHost;
        server.reset(new AgentServer(config));
        server->listen();
    }

    // Connects as `client`, returns the connection once TLS is up (or null).
    std::unique_ptr<PeerConnection> connect(const QString& client)
    {
        QSslSocket* socket = connectPinned("127.0.0.1", server->port(), testCert(client), testKey(client),
                                           testCert("server"));
        QSignalSpy encrypted(socket, &QSslSocket::encrypted);
        if (!encrypted.wait(5000)) {
            delete socket;
            return nullptr;
        }
        return std::unique_ptr<PeerConnection>(new PeerConnection(socket));
    }
};

QJsonObject hello()
{
    return QJsonObject{ { "version", (int)kProtocolVersion }, { "client", "test" } };
}

bool waitForType(QSignalSpy& frames, MsgType type, int timeoutMs = 3000)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < timeoutMs) {
        for (const auto& args : frames) {
            if (args.at(0).value<Frame>().type == type) {
                return true;
            }
        }
        frames.wait(50);
    }
    return false;
}

}

void TestAgentServer::helloAck()
{
    Fixture f;
    auto conn = f.connect("client1");
    QVERIFY(conn);
    QSignalSpy frames(conn.get(), &PeerConnection::frameReceived);
    conn->send(MsgType::Hello, jsonPayload(hello()));
    QVERIFY(waitForType(frames, MsgType::HelloAck));
    const auto ack = parseJsonPayload(frames.last().at(0).value<Frame>().payload);
    QCOMPARE(ack->value("version").toInt(), (int)kProtocolVersion);
}

void TestAgentServer::unpairedClientDropped()
{
    Fixture f;
    auto conn = f.connect("client2");
    if (conn) {
        QSignalSpy frames(conn.get(), &PeerConnection::frameReceived);
        QSignalSpy closed(conn.get(), &PeerConnection::closed);
        conn->send(MsgType::Hello, jsonPayload(hello()));
        conn->send(MsgType::LockNow);
        QVERIFY(closed.wait(3000) || closed.count() > 0);
        QCOMPARE(frames.count(), 0);
    }
    QTest::qWait(100);
    QCOMPARE(f.locks, 0);
}

void TestAgentServer::lockNow()
{
    Fixture f;
    auto conn = f.connect("client1");
    QSignalSpy frames(conn.get(), &PeerConnection::frameReceived);
    conn->send(MsgType::Hello, jsonPayload(hello()));
    QVERIFY(waitForType(frames, MsgType::HelloAck));
    conn->send(MsgType::LockNow);
    QTRY_COMPARE_WITH_TIMEOUT(f.locks, 1, 3000);
}

void TestAgentServer::goodbyeLocksWhenEnabled()
{
    Fixture f;
    auto conn = f.connect("client1");
    QSignalSpy frames(conn.get(), &PeerConnection::frameReceived);
    conn->send(MsgType::Hello, jsonPayload(hello()));
    QVERIFY(waitForType(frames, MsgType::HelloAck));
    conn->send(MsgType::SetLockOnDisconnect, jsonPayload(QJsonObject{ { "enabled", true } }));
    conn->send(MsgType::Goodbye);
    QTRY_COMPARE_WITH_TIMEOUT(f.locks, 1, 3000);
}

void TestAgentServer::goodbyeNoLockWhenDisabled()
{
    Fixture f;
    auto conn = f.connect("client1");
    QSignalSpy frames(conn.get(), &PeerConnection::frameReceived);
    conn->send(MsgType::Hello, jsonPayload(hello()));
    QVERIFY(waitForType(frames, MsgType::HelloAck));
    conn->send(MsgType::Goodbye);
    conn.reset();
    QTest::qWait(600);
    QCOMPARE(f.locks, 0);
}

void TestAgentServer::dropLocksAfterGrace()
{
    Fixture f;
    auto conn = f.connect("client1");
    QSignalSpy frames(conn.get(), &PeerConnection::frameReceived);
    conn->send(MsgType::Hello, jsonPayload(hello()));
    QVERIFY(waitForType(frames, MsgType::HelloAck));
    conn->send(MsgType::SetLockOnDisconnect, jsonPayload(QJsonObject{ { "enabled", true } }));
    QTest::qWait(100);
    conn.reset(); // drops without Goodbye (Wi-Fi gone)
    QTest::qWait(100);
    QCOMPARE(f.locks, 0); // not before the grace period
    QTRY_COMPARE_WITH_TIMEOUT(f.locks, 1, 2000);
}

void TestAgentServer::reconnectWithinGraceCancelsLock()
{
    Fixture f;
    {
        auto conn = f.connect("client1");
        QSignalSpy frames(conn.get(), &PeerConnection::frameReceived);
        conn->send(MsgType::Hello, jsonPayload(hello()));
        QVERIFY(waitForType(frames, MsgType::HelloAck));
        conn->send(MsgType::SetLockOnDisconnect, jsonPayload(QJsonObject{ { "enabled", true } }));
        QTest::qWait(100);
    }
    auto again = f.connect("client1");
    QVERIFY(again);
    QSignalSpy frames(again.get(), &PeerConnection::frameReceived);
    again->send(MsgType::Hello, jsonPayload(hello()));
    QVERIFY(waitForType(frames, MsgType::HelloAck));
    QTest::qWait(600);
    QCOMPARE(f.locks, 0);
}

void TestAgentServer::unpairWhileRunning()
{
    Fixture f;
    auto first = f.connect("client1");
    QVERIFY(first);
    f.state = sunshineState({ { testCert("client1"), "false" } }); // unpaired in Sunshine
    auto second = f.connect("client1");
    if (second) {
        QSignalSpy frames(second.get(), &PeerConnection::frameReceived);
        QSignalSpy closed(second.get(), &PeerConnection::closed);
        second->send(MsgType::Hello, jsonPayload(hello()));
        QVERIFY(closed.wait(3000) || closed.count() > 0);
        QCOMPARE(frames.count(), 0);
    }
}

void TestAgentServer::silentClientDroppedAndLocks()
{
    // Wi-Fi gone for good: TCP stays "open" on the PC but nothing arrives any more
    Fixture f(400);
    auto conn = f.connect("client1");
    QSignalSpy frames(conn.get(), &PeerConnection::frameReceived);
    conn->send(MsgType::Hello, jsonPayload(QJsonObject{ { "version", 1 }, { "lockOnDisconnect", true } }));
    QVERIFY(waitForType(frames, MsgType::HelloAck));
    // no Ping from here on, socket left open
    QTRY_COMPARE_WITH_TIMEOUT(f.locks, 1, 3000);
}

void TestAgentServer::pingsKeepSessionAlive()
{
    Fixture f(400);
    auto conn = f.connect("client1");
    QSignalSpy frames(conn.get(), &PeerConnection::frameReceived);
    conn->send(MsgType::Hello, jsonPayload(QJsonObject{ { "version", 1 }, { "lockOnDisconnect", true } }));
    QVERIFY(waitForType(frames, MsgType::HelloAck));
    QSignalSpy closed(conn.get(), &PeerConnection::closed);
    for (int i = 0; i < 12; i++) {
        conn->send(MsgType::Ping);
        QTest::qWait(100);
    }
    QCOMPARE(closed.count(), 0);
    QCOMPARE(f.locks, 0);
}

void TestAgentServer::reconnectThenDropStillLocks()
{
    // Flaky Wi-Fi: drop, quick reconnect that dies before saying anything, drop again
    Fixture f;
    {
        auto conn = f.connect("client1");
        QSignalSpy frames(conn.get(), &PeerConnection::frameReceived);
        conn->send(MsgType::Hello, jsonPayload(QJsonObject{ { "version", 1 }, { "lockOnDisconnect", true } }));
        QVERIFY(waitForType(frames, MsgType::HelloAck));
    }
    {
        auto again = f.connect("client1"); // TLS only, no Hello
        QVERIFY(again);
        QTest::qWait(50);
    }
    QTRY_COMPARE_WITH_TIMEOUT(f.locks, 1, 3000);
}
