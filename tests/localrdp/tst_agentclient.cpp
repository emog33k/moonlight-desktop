#include "tests.h"
#include "agentserver.h"
#include "streaming/localrdp/agentclient.h"

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
                                                        testCert("server"), port, 200));
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
