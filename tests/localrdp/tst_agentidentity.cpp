#include "tests.h"
#include "localrdp-shared/agentidentity.h"

#include <QSslKey>
#include <QTest>

using namespace LocalRdp;

QSslCertificate testCert(const QString& name);
QSslKey testKey(const QString& name);

static AgentIdentity issueFrom(const QString& name)
{
    const auto id = issueAgentIdentity(testCert(name).toPem(), testKey(name).toPem());
    return id ? *id : AgentIdentity();
}

void TestAgentIdentity::issuedCertificateIsUsable()
{
    const AgentIdentity id = issueFrom("server");
    const QSslCertificate cert(id.certPem, QSsl::Pem);
    QVERIFY(!cert.isNull());
    QCOMPARE(cert.subjectInfo(QSslCertificate::CommonName), QStringList({ kAgentCertCommonName }));
    QCOMPARE(cert.issuerDisplayName(), testCert("server").subjectDisplayName());
    QVERIFY(!QSslKey(id.keyPem, QSsl::Ec, QSsl::Pem).isNull());
    // A fresh key every time
    QVERIFY(issueFrom("server").keyPem != id.keyPem);
}

void TestAgentIdentity::pinnedCertificateItselfTrusted()
{
    QVERIFY(isTrustedServerCertificate(testCert("server"), testCert("server")));
}

void TestAgentIdentity::agentSignedByPinnedTrusted()
{
    const QSslCertificate agent(issueFrom("server").certPem, QSsl::Pem);
    QVERIFY(isTrustedServerCertificate(agent, testCert("server")));
}

void TestAgentIdentity::agentSignedByOtherKeyRejected()
{
    // Signed by a different key (another device's): not ours
    const QSslCertificate agent(issueFrom("client2").certPem, QSsl::Pem);
    QVERIFY(!isTrustedServerCertificate(agent, testCert("server")));
}

void TestAgentIdentity::otherCertificatesRejected()
{
    QVERIFY(!isTrustedServerCertificate(testCert("client1"), testCert("server")));
    QVERIFY(!isTrustedServerCertificate(QSslCertificate(), testCert("server")));
    QVERIFY(!isTrustedServerCertificate(testCert("server"), QSslCertificate()));
    const QSslCertificate agent(issueFrom("server").certPem, QSsl::Pem);
    QVERIFY(!isTrustedServerCertificate(agent, QSslCertificate()));
}

void TestAgentIdentity::garbageInputFails()
{
    QVERIFY(!issueAgentIdentity("not a cert", testKey("server").toPem()));
    QVERIFY(!issueAgentIdentity(testCert("server").toPem(), "not a key"));
    // Key that does not belong to the certificate
    QVERIFY(!issueAgentIdentity(testCert("server").toPem(), testKey("client1").toPem()));
}
