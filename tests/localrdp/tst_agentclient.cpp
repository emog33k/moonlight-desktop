#include "tests.h"
#include "agentserver.h"
#include "streaming/localrdp/agentclient.h"
#include "localrdp-shared/agentidentity.h"
#include "streaming/localrdp/sessionmenuhost.h"
#include "settings/streamingpreferences.h"

#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <atomic>
#include <mutex>
#include <QJsonObject>
#include <QSslServer>
#include <QTcpServer>
#include "localrdp-shared/peerconnection.h"
#include <QTest>

#include <memory>

using namespace LocalRdp;

QSslCertificate testCert(const QString& name);
QSslKey testKey(const QString& name);
QByteArray sunshineState(const QList<QPair<QSslCertificate, QString>>& devices);

namespace {

struct AgentFixture {
    int locks = 0;
    std::unique_ptr<AgentServer> server;

    explicit AgentFixture(const QString& serverCertName = "server", int idleTimeoutMs = 60000)
    {
        AgentConfig config;
        config.useSystemClipboard = false; // never the real clipboard in tests
        config.serverCert = testCert(serverCertName);
        config.serverKey = testKey(serverCertName);
        config.readSunshineState = []() { return sunshineState({ { testCert("client1"), "true" } }); };
        config.lockWorkstation = [this]() { locks++; };
        config.reconnectGraceMs = 300;
        config.idleTimeoutMs = idleTimeoutMs;
        config.port = 0;
        config.bindAddress = QHostAddress::LocalHost;
        server.reset(new AgentServer(config));
        server->listen();
    }
};

std::unique_ptr<AgentClient> makeClient(quint16 port, const QString& clientName = "client1")
{
    return std::unique_ptr<AgentClient>(new AgentClient("127.0.0.1", testCert(clientName), testKey(clientName),
                                                        testCert("server"), port, 200, 2000, 6000, /*useSystemClipboard*/ false));
}

}

void TestAgentClient::connectsAndReportsConnected()
{
    AgentFixture agent;
    auto client = makeClient(agent.server->port());
    QTRY_COMPARE_WITH_TIMEOUT(client->status(), AgentStatus::Connected, 5000);
}

void TestAgentClient::notFoundWhenNoServer()
{
    QTcpServer probe;
    probe.listen(QHostAddress::LocalHost, 0);
    const quint16 freePort = probe.serverPort();
    probe.close(); // nothing listens here now

    auto client = makeClient(freePort);
    QTRY_COMPARE_WITH_TIMEOUT(client->status(), AgentStatus::NotFound, 5000);
}

void TestAgentClient::certErrorWhenServerCertDiffers()
{
    AgentFixture impostor("client2"); // presents a certificate that is not the pinned one
    auto client = makeClient(impostor.server->port());
    QTRY_COMPARE_WITH_TIMEOUT(client->status(), AgentStatus::CertError, 5000);
}

void TestAgentClient::rejectedWhenNotPaired()
{
    AgentFixture agent;
    auto client = makeClient(agent.server->port(), "client2");
    QTRY_COMPARE_WITH_TIMEOUT(client->status(), AgentStatus::Rejected, 5000);
}

void TestAgentClient::lockNowReachesServer()
{
    AgentFixture agent;
    auto client = makeClient(agent.server->port());
    QTRY_COMPARE_WITH_TIMEOUT(client->status(), AgentStatus::Connected, 5000);
    client->lockNow();
    QTRY_COMPARE_WITH_TIMEOUT(agent.locks, 1, 3000);
}

void TestAgentClient::goodbyeWithLockOnDisconnect()
{
    AgentFixture agent;
    auto client = makeClient(agent.server->port());
    client->setLockOnDisconnect(true); // before the connection exists: sent once connected
    QTRY_COMPARE_WITH_TIMEOUT(client->status(), AgentStatus::Connected, 5000);
    client->sayGoodbye();
    client.reset();
    QTRY_COMPARE_WITH_TIMEOUT(agent.locks, 1, 3000);
}

void TestAgentClient::reconnectsAfterAgentRestart()
{
    auto agent = std::make_unique<AgentFixture>();
    const quint16 port = agent->server->port();
    auto client = makeClient(port);
    QTRY_COMPARE_WITH_TIMEOUT(client->status(), AgentStatus::Connected, 5000);

    agent.reset(); // agent stopped
    QTRY_VERIFY_WITH_TIMEOUT(client->status() != AgentStatus::Connected, 5000);

    // Agent comes back on the same port
    AgentConfig config;
    config.useSystemClipboard = false; // never the real clipboard in tests
    config.serverCert = testCert("server");
    config.serverKey = testKey("server");
    config.readSunshineState = []() { return sunshineState({ { testCert("client1"), "true" } }); };
    config.port = port;
    config.bindAddress = QHostAddress::LocalHost;
    AgentServer again(config);
    QVERIFY(again.listen());
    QTRY_COMPARE_WITH_TIMEOUT(client->status(), AgentStatus::Connected, 5000);
}

void TestAgentClient::destructorReturnsFast()
{
    AgentFixture agent;
    auto client = makeClient(agent.server->port());
    QTRY_COMPARE_WITH_TIMEOUT(client->status(), AgentStatus::Connected, 5000);
    QElapsedTimer t;
    t.start();
    client.reset();
    QVERIFY2(t.elapsed() < 1000, qPrintable(QString("took %1 ms").arg(t.elapsed())));
}

void TestAgentClient::deadServerDetected()
{
    // A server that completes TLS and Hello, then never answers again (PC hung or gone)
    QSslServer server;
    QSslConfiguration ssl = QSslConfiguration::defaultConfiguration();
    ssl.setLocalCertificate(testCert("server"));
    ssl.setPrivateKey(testKey("server"));
    ssl.setPeerVerifyMode(QSslSocket::VerifyPeer);
    server.setSslConfiguration(ssl);
    QObject::connect(&server, &QSslServer::sslErrors, &server, [](QSslSocket* s, const QList<QSslError>& e) { s->ignoreSslErrors(e); });
    QObject::connect(&server, &QTcpServer::pendingConnectionAvailable, &server, [&server]() {
        auto conn = new PeerConnection(qobject_cast<QSslSocket*>(server.nextPendingConnection()), &server);
        QObject::connect(conn, &PeerConnection::frameReceived, conn, [conn](const Proto::Frame& f) {
            if (f.type == Proto::MsgType::Hello) {
                conn->send(Proto::MsgType::HelloAck, Proto::jsonPayload(QJsonObject{ { "version", 1 } }));
            } // Ping ignored
        });
    });
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));

    AgentClient client("127.0.0.1", testCert("client1"), testKey("client1"), testCert("server"),
                       server.serverPort(), 5000, 100, 400);
    QTRY_COMPARE_WITH_TIMEOUT(client.status(), AgentStatus::Connected, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(client.status() != AgentStatus::Connected, 3000);
}

void TestAgentClient::liveSessionStaysConnected()
{
    AgentFixture agent("server", 400); // short idle timeout on the agent
    AgentClient client("127.0.0.1", testCert("client1"), testKey("client1"), testCert("server"),
                       agent.server->port(), 200, 100, 1000);
    client.setLockOnDisconnect(true);
    QTRY_COMPARE_WITH_TIMEOUT(client.status(), AgentStatus::Connected, 5000);
    QTest::qWait(1500);
    QCOMPARE(client.status(), AgentStatus::Connected);
    QCOMPARE(agent.locks, 0);
}

void TestAgentClient::filesOfferedFromLaptopReachThePc()
{
    std::mutex mutex;
    QStringList offered;
    AgentConfig config;
    config.useSystemClipboard = false; // never the real clipboard in tests
    config.serverCert = testCert("server");
    config.serverKey = testKey("server");
    config.readSunshineState = []() { return sunshineState({ { testCert("client1"), "true" } }); };
    config.port = 0;
    config.bindAddress = QHostAddress::LocalHost;
    config.useSystemClipboard = false;
    config.onPeerOffer = [&](const Clip::Offer& o) {
        std::lock_guard<std::mutex> lock(mutex);
        for (const auto& f : o.files) offered << f.name;
    };
    AgentServer server(config);
    QVERIFY(server.listen());

    QTemporaryDir dir;
    QFile f(dir.filePath("Отчёт.docx"));
    f.open(QIODevice::WriteOnly);
    f.write("doc");
    f.close();

    AgentClient client("127.0.0.1", testCert("client1"), testKey("client1"), testCert("server"),
                       server.port(), 200, 2000, 6000, /*useSystemClipboard*/ false);
    QTRY_COMPARE_WITH_TIMEOUT(client.status(), AgentStatus::Connected, 5000);
    client.offerFiles({ f.fileName() });
    QTRY_VERIFY_WITH_TIMEOUT([&]() { std::lock_guard<std::mutex> lock(mutex); return offered.contains("Отчёт.docx"); }(), 5000);
}

void TestAgentClient::clipboardSyncOffSendsNothing()
{
    std::atomic<int> offers { 0 };
    AgentConfig config;
    config.useSystemClipboard = false; // never the real clipboard in tests
    config.serverCert = testCert("server");
    config.serverKey = testKey("server");
    config.readSunshineState = []() { return sunshineState({ { testCert("client1"), "true" } }); };
    config.port = 0;
    config.bindAddress = QHostAddress::LocalHost;
    config.useSystemClipboard = false;
    config.onPeerOffer = [&](const Clip::Offer&) { offers++; };
    AgentServer server(config);
    QVERIFY(server.listen());

    QTemporaryDir dir;
    QFile f(dir.filePath("a.txt"));
    f.open(QIODevice::WriteOnly);
    f.close();

    AgentClient client("127.0.0.1", testCert("client1"), testKey("client1"), testCert("server"),
                       server.port(), 200, 2000, 6000, false);
    client.setClipboardSync(false);
    QTRY_COMPARE_WITH_TIMEOUT(client.status(), AgentStatus::Connected, 5000);
    client.offerFiles({ f.fileName() });
    QTest::qWait(500);
    QCOMPARE(offers.load(), 0);
}

namespace {
class MenuInput : public StreamMenuHost
{
public:
    QVector<int> params;
    StreamState queryState() override { return StreamState(); }
    void performAction(StreamAction, int param) override { params << param; }
    bool isCaptureActive() override { return true; }
    void setCaptureActive(bool) override {}
    void suspendPointerLock(bool) override {}
};
}

void TestAgentClient::pcDisplaysShownInMenu()
{
    AgentConfig config;
    config.useSystemClipboard = false; // never the real clipboard in tests
    config.serverCert = testCert("server");
    config.serverKey = testKey("server");
    config.readSunshineState = []() { return sunshineState({ { testCert("client1"), "true" } }); };
    config.port = 0;
    config.bindAddress = QHostAddress::LocalHost;
    config.listDisplays = []() {
        Proto::DisplayList l;
        l.displays = { { "\\.\DISPLAY1", 2560, 1440, false }, { "\\.\DISPLAY2", 1920, 1080, true } };
        l.initial = 1;
        return l;
    };
    AgentServer server(config);
    QVERIFY(server.listen());

    auto client = makeClient(server.port());
    QTRY_COMPARE_WITH_TIMEOUT(client->displays().size(), 2, 5000);
    QCOMPARE(client->displays()[0].width, 2560);
    QCOMPARE(client->displays()[1].primary, true);
    QCOMPARE(client->initialDisplay(), 1);

    // The menu marks the monitor Sunshine started on until the user picks another
    MenuInput input;
    SessionMenuHost host(&input, StreamingPreferences::get(), client.get());
    QCOMPARE(host.queryState().displays.size(), 2);
    QCOMPARE(host.queryState().currentDisplay, 1);
    host.performAction(StreamAction::SwitchDisplay, 0);
    QCOMPARE(host.queryState().currentDisplay, 0);
    QCOMPARE(input.params, QVector<int>({ 0 }));
}

void TestAgentClient::pcCursorReachesClient()
{
    Proto::CursorState cursor;
    cursor.kind = Proto::CursorKind::Shape;
    cursor.key = 7;
    cursor.image.width = cursor.image.height = 3;
    cursor.image.hotX = 1;
    cursor.image.bgra = QByteArray(36, '\x10');

    AgentConfig config;
    config.useSystemClipboard = false; // never the real clipboard in tests
    config.serverCert = testCert("server");
    config.serverKey = testKey("server");
    config.readSunshineState = []() { return sunshineState({ { testCert("client1"), "true" } }); };
    config.port = 0;
    config.bindAddress = QHostAddress::LocalHost;
    config.readCursor = [&cursor]() { return cursor; };
    config.cursorPollMs = 20;
    config.sunshineInstance = []() { return QString("999"); };
    AgentServer server(config);
    QVERIFY(server.listen());

    auto client = makeClient(server.port());
    std::atomic<int> changes { 0 };
    client->setChangeHandler([&changes]() { changes++; });
    QTRY_COMPARE_WITH_TIMEOUT(client->status(), AgentStatus::Connected, 5000);
    QCOMPARE(client->sunshineInstance(), QString("999"));

    // (QTRY_* evaluates its expression once more at the end, so wait by hand)
    auto waitCursor = [&client]() {
        std::optional<Proto::CursorState> c;
        QElapsedTimer t;
        t.start();
        while (!(c = client->takeCursor()) && t.elapsed() < 3000) {
            QTest::qWait(20);
        }
        return c;
    };
    std::optional<Proto::CursorState> got = waitCursor();
    QVERIFY(got);
    QCOMPARE(got->kind, Proto::CursorKind::Shape);
    QVERIFY(got->image == cursor.image);
    QVERIFY(!client->takeCursor()); // taken: nothing new
    QVERIFY(changes > 0);

    cursor.kind = Proto::CursorKind::Hidden;
    got = waitCursor();
    QVERIFY(got);
    QCOMPARE(got->kind, Proto::CursorKind::Hidden);
}

namespace {
std::unique_ptr<AgentServer> serverWithIdentity(const QString& signerName)
{
    const auto id = issueAgentIdentity(testCert(signerName).toPem(), testKey(signerName).toPem());
    if (!id) {
        return nullptr;
    }
    AgentConfig config;
    config.useSystemClipboard = false; // never the real clipboard in tests
    config.serverCert = QSslCertificate(id->certPem, QSsl::Pem);
    config.serverKey = QSslKey(id->keyPem, QSsl::Ec, QSsl::Pem);
    config.readSunshineState = []() { return sunshineState({ { testCert("client1"), "true" } }); };
    config.port = 0;
    config.bindAddress = QHostAddress::LocalHost;
    std::unique_ptr<AgentServer> server(new AgentServer(config));
    return server->listen() ? std::move(server) : nullptr;
}
}

void TestAgentClient::connectsToAgentWithOwnIdentity()
{
    // The agent shows its own certificate, signed by Sunshine's key (pinned as "server")
    auto server = serverWithIdentity("server");
    QVERIFY(server);
    auto client = makeClient(server->port());
    QTRY_COMPARE_WITH_TIMEOUT(client->status(), AgentStatus::Connected, 5000);
}

void TestAgentClient::agentIdentityFromOtherKeyRejected()
{
    auto server = serverWithIdentity("client2");
    QVERIFY(server);
    auto client = makeClient(server->port());
    QTRY_COMPARE_WITH_TIMEOUT(client->status(), AgentStatus::CertError, 5000);
    QTest::qWait(500);
    QCOMPARE(client->status(), AgentStatus::CertError);
}

void TestAgentClient::silentServerRetried()
{
    // Something accepts the TCP connection but never completes TLS or answers
    // Hello (PC asleep mid-handshake, agent hung): the client must give up and
    // try again instead of waiting forever.
    QTcpServer silent;
    QVERIFY(silent.listen(QHostAddress::LocalHost, 0));
    int accepted = 0;
    QList<QTcpSocket*> held;
    QObject::connect(&silent, &QTcpServer::newConnection, [&]() {
        while (silent.hasPendingConnections()) {
            held << silent.nextPendingConnection();
            accepted++;
        }
    });
    std::unique_ptr<AgentClient> client(new AgentClient("127.0.0.1", testCert("client1"), testKey("client1"),
                                                        testCert("server"), silent.serverPort(), 200, 2000, 500, false));
    QTRY_VERIFY_WITH_TIMEOUT(accepted >= 2, 5000);
    QVERIFY(client->status() != AgentStatus::Connected);
}

void TestAgentClient::laptopClipboardAgeAcrossStreams()
{
    // Unchanged since the last stream: the time we knew then
    QCOMPARE(laptopClipboardCopyTime(42, 42, 1000, 5000), qint64(1000));
    // Changed while not streaming: copied just now, as far as we can tell
    QCOMPARE(laptopClipboardCopyTime(43, 42, 1000, 5000), qint64(5000));
    // Never streamed before
    QCOMPARE(laptopClipboardCopyTime(43, 0, 0, 5000), qint64(5000));
}

void TestAgentClient::switchingMonitorRefreshesPcScreens()
{
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
    auto client = makeClient(server.port());
    QTRY_COMPARE_WITH_TIMEOUT(client->status(), AgentStatus::Connected, 5000);

    MenuInput input;
    SessionMenuHost host(&input, StreamingPreferences::get(), client.get());
    host.performAction(StreamAction::SwitchDisplay, 1);
    QCOMPARE(input.params, QVector<int>({ 1 })); // Ctrl+Alt+Shift+F2 still goes to Sunshine
    QTRY_COMPARE_WITH_TIMEOUT(nudges.load(), 3, 3000);
}
