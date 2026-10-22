#include "tests.h"
#include "agentserver.h"
#include "localrdp-shared/peerconnection.h"
#include "localrdp-shared/hostdisplays.h"
#include "localrdp-shared/cursorshape.h"
#include "localrdp-shared/clip/clipmessages.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTest>

#include <atomic>
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
        config.useSystemClipboard = false; // never the real clipboard in tests
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

void TestAgentServer::displayListAfterHelloAndOnChange()
{
    DisplayList displays;
    displays.displays = { { "\\.\DISPLAY1", 1920, 1080, true } };
    displays.initial = 0;

    AgentConfig config;
    config.useSystemClipboard = false; // never the real clipboard in tests
    config.serverCert = testCert("server");
    config.serverKey = testKey("server");
    config.readSunshineState = []() { return sunshineState({ { testCert("client1"), "true" } }); };
    config.port = 0;
    config.bindAddress = QHostAddress::LocalHost;
    config.listDisplays = [&displays]() { return displays; };
    config.displayPollMs = 100;
    AgentServer server(config);
    QVERIFY(server.listen());

    QSslSocket* socket = connectPinned("127.0.0.1", server.port(), testCert("client1"), testKey("client1"), testCert("server"));
    QSignalSpy encrypted(socket, &QSslSocket::encrypted);
    QVERIFY(encrypted.wait(5000));
    PeerConnection conn(socket);
    QSignalSpy frames(&conn, &PeerConnection::frameReceived);

    // Nothing about the PC before Hello
    QTest::qWait(300);
    QCOMPARE(frames.count(), 0);

    conn.send(MsgType::Hello, jsonPayload(hello()));
    QVERIFY(waitForType(frames, MsgType::DisplayList));
    auto lastList = [&frames]() {
        std::optional<DisplayList> last;
        for (const auto& args : frames) {
            const Frame f = args.at(0).value<Frame>();
            if (f.type == MsgType::DisplayList) {
                last = decodeDisplayList(f.payload);
            }
        }
        return last;
    };
    QVERIFY(lastList() && *lastList() == displays);

    // Unchanged: not sent again
    const int before = frames.count();
    QTest::qWait(400);
    int lists = 0;
    for (int i = before; i < frames.count(); i++) {
        lists += frames.at(i).at(0).value<Frame>().type == MsgType::DisplayList;
    }
    QCOMPARE(lists, 0);

    // A monitor plugged in: sent again
    displays.displays << HostDisplay{ "\\.\DISPLAY2", 1280, 1024, false };
    QTRY_VERIFY_WITH_TIMEOUT(lastList() && lastList()->displays.size() == 2, 3000);
}

void TestAgentServer::cursorAndSunshineAfterHello()
{
    CursorState cursor;
    cursor.kind = CursorKind::Shape;
    cursor.key = 1;
    cursor.image.width = cursor.image.height = 2;
    cursor.image.bgra = QByteArray(16, '\x7f');

    AgentConfig config;
    config.useSystemClipboard = false; // never the real clipboard in tests
    config.serverCert = testCert("server");
    config.serverKey = testKey("server");
    config.readSunshineState = []() { return sunshineState({ { testCert("client1"), "true" } }); };
    config.port = 0;
    config.bindAddress = QHostAddress::LocalHost;
    config.readCursor = [&cursor]() { return cursor; };
    config.cursorPollMs = 20;
    config.sunshineInstance = []() { return QString("4321"); };
    AgentServer server(config);
    QVERIFY(server.listen());

    QSslSocket* socket = connectPinned("127.0.0.1", server.port(), testCert("client1"), testKey("client1"), testCert("server"));
    QSignalSpy encrypted(socket, &QSslSocket::encrypted);
    QVERIFY(encrypted.wait(5000));
    PeerConnection conn(socket);
    QSignalSpy frames(&conn, &PeerConnection::frameReceived);
    QTest::qWait(150);
    QCOMPARE(frames.count(), 0); // nothing before Hello

    conn.send(MsgType::Hello, jsonPayload(hello()));
    QVERIFY(waitForType(frames, MsgType::CursorShape));
    auto count = [&frames](MsgType type) {
        int n = 0;
        for (const auto& args : frames) {
            n += args.at(0).value<Frame>().type == type;
        }
        return n;
    };
    for (const auto& args : frames) {
        const Frame f = args.at(0).value<Frame>();
        if (f.type == MsgType::HelloAck) {
            QCOMPARE(parseJsonPayload(f.payload)->value("sunshine").toString(), QString("4321"));
        }
        if (f.type == MsgType::CursorShape) {
            QVERIFY(decodeCursorImage(f.payload) && *decodeCursorImage(f.payload) == cursor.image);
        }
    }

    // Unchanged: sent once only
    QTest::qWait(200);
    QCOMPARE(count(MsgType::CursorShape), 1);

    cursor.kind = CursorKind::Hidden;
    QVERIFY(waitForType(frames, MsgType::CursorHidden));
    cursor.kind = CursorKind::Default;
    QVERIFY(waitForType(frames, MsgType::CursorDefault));
    cursor.kind = CursorKind::Shape;
    cursor.key = 2;
    QTRY_COMPARE_WITH_TIMEOUT(count(MsgType::CursorShape), 2, 3000);
}

void TestAgentServer::noClipboardWhilePcLocked()
{
    std::atomic<bool> locked { true };
    std::atomic<int> peerOffers { 0 };
    AgentConfig config;
    config.useSystemClipboard = false; // never the real clipboard in tests
    config.serverCert = testCert("server");
    config.serverKey = testKey("server");
    config.readSunshineState = []() { return sunshineState({ { testCert("client1"), "true" } }); };
    config.port = 0;
    config.bindAddress = QHostAddress::LocalHost;
    config.useSystemClipboard = false;
    config.isSessionLocked = [&locked]() { return locked.load(); };
    config.lockPollMs = 50;
    config.onPeerOffer = [&peerOffers](const Clip::Offer&) { peerOffers++; };
    AgentServer server(config);
    QVERIFY(server.listen());

    QSslSocket* socket = connectPinned("127.0.0.1", server.port(), testCert("client1"), testKey("client1"), testCert("server"));
    QSignalSpy encrypted(socket, &QSslSocket::encrypted);
    QVERIFY(encrypted.wait(5000));
    PeerConnection conn(socket);
    QSignalSpy frames(&conn, &PeerConnection::frameReceived);
    conn.send(MsgType::Hello, jsonPayload(hello()));
    QVERIFY(waitForType(frames, MsgType::HelloAck));

    Clip::Offer offer;
    offer.seq = 1;
    offer.textBytes = 4;
    conn.send(MsgType::ClipOffer, Clip::encodeOffer(offer));
    QTest::qWait(300);
    QCOMPARE(peerOffers.load(), 0); // nothing from the laptop lands on a locked PC

    locked = false;
    QTest::qWait(200);
    offer.seq = 2;
    conn.send(MsgType::ClipOffer, Clip::encodeOffer(offer));
    QTRY_COMPARE_WITH_TIMEOUT(peerOffers.load(), 1, 3000);
}

void TestAgentServer::replacedThenSilentDropStillLocks()
{
    // The laptop's Wi-Fi drops; its reconnect completes TLS (replacing the old
    // session) but dies before Hello: the PC must still lock after the grace time
    Fixture f;
    auto first = f.connect("client1");
    QSignalSpy frames(first.get(), &PeerConnection::frameReceived);
    first->send(MsgType::Hello, jsonPayload(QJsonObject{ { "version", 1 }, { "lockOnDisconnect", true } }));
    QVERIFY(waitForType(frames, MsgType::HelloAck));
    {
        auto again = f.connect("client1"); // replaces the first, no Hello
        QVERIFY(again);
        QTest::qWait(100);
    }
    first.reset();
    QTRY_COMPARE_WITH_TIMEOUT(f.locks, 1, 3000);
}

void TestAgentServer::refreshScreensAfterSwitch()
{
    // After a monitor switch Sunshine only sends a frame once something on the
    // screen changes: the agent nudges the screens a few times
    std::atomic<int> nudges { 0 };
    AgentConfig config;
    config.useSystemClipboard = false;
    config.serverCert = testCert("server");
    config.serverKey = testKey("server");
    config.readSunshineState = []() { return sunshineState({ { testCert("client1"), "true" } }); };
    config.port = 0;
    config.bindAddress = QHostAddress::LocalHost;
    config.refreshScreens = [&nudges]() { nudges++; };
    config.refreshDelaysMs = { 10, 60, 120 };
    AgentServer server(config);
    QVERIFY(server.listen());

    QSslSocket* socket = connectPinned("127.0.0.1", server.port(), testCert("client1"), testKey("client1"), testCert("server"));
    QSignalSpy encrypted(socket, &QSslSocket::encrypted);
    QVERIFY(encrypted.wait(5000));
    PeerConnection conn(socket);
    QSignalSpy frames(&conn, &PeerConnection::frameReceived);
    conn.send(MsgType::RefreshScreens); // before Hello: ignored
    QTest::qWait(200);
    QCOMPARE(nudges.load(), 0);
    conn.send(MsgType::Hello, jsonPayload(hello()));
    QVERIFY(waitForType(frames, MsgType::HelloAck));
    conn.send(MsgType::RefreshScreens);
    QTRY_COMPARE_WITH_TIMEOUT(nudges.load(), 3, 3000);
}

void TestAgentServer::pcLockStateReported()
{
    std::atomic<bool> locked { false };
    AgentConfig config;
    config.useSystemClipboard = false;
    config.serverCert = testCert("server");
    config.serverKey = testKey("server");
    config.readSunshineState = []() { return sunshineState({ { testCert("client1"), "true" } }); };
    config.port = 0;
    config.bindAddress = QHostAddress::LocalHost;
    config.isSessionLocked = [&locked]() { return locked.load(); };
    config.lockPollMs = 30;
    AgentServer server(config);
    QVERIFY(server.listen());
    QSslSocket* socket = connectPinned("127.0.0.1", server.port(), testCert("client1"), testKey("client1"), testCert("server"));
    QSignalSpy encrypted(socket, &QSslSocket::encrypted);
    QVERIFY(encrypted.wait(5000));
    PeerConnection conn(socket);
    QSignalSpy frames(&conn, &PeerConnection::frameReceived);
    conn.send(MsgType::Hello, jsonPayload(hello()));
    QVERIFY(waitForType(frames, MsgType::PcState));
    auto lastLocked = [&frames]() {
        int value = -1;
        for (const auto& args : frames) {
            const Frame f = args.at(0).value<Frame>();
            if (f.type == MsgType::PcState) value = parseJsonPayload(f.payload)->value("locked").toBool() ? 1 : 0;
        }
        return value;
    };
    QCOMPARE(lastLocked(), 0);
    locked = true;
    QTRY_COMPARE_WITH_TIMEOUT(lastLocked(), 1, 3000);
}
