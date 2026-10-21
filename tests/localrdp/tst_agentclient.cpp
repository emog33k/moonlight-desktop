#include "tests.h"
#include "agentserver.h"
#include "streaming/localrdp/agentclient.h"

#include <QElapsedTimer>
#include <QTcpServer>
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

    explicit AgentFixture(const QString& serverCertName = "server")
    {
        AgentConfig config;
        config.serverCert = testCert(serverCertName);
        config.serverKey = testKey(serverCertName);
        config.readSunshineState = []() { return sunshineState({ { testCert("client1"), "true" } }); };
        config.lockWorkstation = [this]() { locks++; };
        config.reconnectGraceMs = 300;
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
