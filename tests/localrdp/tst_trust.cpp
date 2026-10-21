#include "tests.h"
#include "localrdp-shared/trust.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSslCertificate>
#include <QTest>

using namespace LocalRdp;

QSslCertificate testCert(const QString& name)
{
    QFile f(":/testdata/" + name + ".crt");
    f.open(QIODevice::ReadOnly);
    return QSslCertificate(f.readAll(), QSsl::Pem);
}

// Same shape as Sunshine's sunshine_state.json
QByteArray sunshineState(const QList<QPair<QSslCertificate, QString>>& devices)
{
    QJsonArray named;
    for (const auto& d : devices) {
        named.append(QJsonObject{
            { "name", "laptop" },
            { "cert", QString::fromLatin1(d.first.toPem()) },
            { "uuid", "00000000-0000-0000-0000-000000000000" },
            { "enabled", d.second },
        });
    }
    QJsonObject root{ { "uniqueid", "x" }, { "named_devices", named } };
    return QJsonDocument(QJsonObject{ { "username", "u" }, { "root", root } }).toJson();
}

void TestTrust::pairedEnabledAccepted()
{
    const QByteArray state = sunshineState({ { testCert("client2"), "true" }, { testCert("client1"), "true" } });
    QVERIFY(isPairedClient(state, testCert("client1")));
}

void TestTrust::pairedDisabledRejected()
{
    QVERIFY(!isPairedClient(sunshineState({ { testCert("client1"), "false" } }), testCert("client1")));
}

void TestTrust::unknownRejected()
{
    QVERIFY(!isPairedClient(sunshineState({ { testCert("client1"), "true" } }), testCert("client2")));
}

void TestTrust::nullCertRejected()
{
    QVERIFY(!isPairedClient(sunshineState({ { testCert("client1"), "true" } }), QSslCertificate()));
}

void TestTrust::garbageJsonRejected()
{
    QVERIFY(!isPairedClient("{not json", testCert("client1")));
    QVERIFY(!isPairedClient(QByteArray(), testCert("client1")));
}

void TestTrust::missingRootRejected()
{
    QVERIFY(!isPairedClient("{\"username\":\"u\"}", testCert("client1")));
    QVERIFY(!isPairedClient("{\"root\":{\"named_devices\":\"oops\"}}", testCert("client1")));
}

void TestTrust::dpapiRoundTrip()
{
    const QByteArray secret("-----BEGIN PRIVATE KEY-----\nsecret\n");
    const auto blob = protectForCurrentUser(secret);
    QVERIFY(blob.has_value());
    QVERIFY(!blob->contains("secret"));
    const auto plain = unprotectForCurrentUser(*blob);
    QVERIFY(plain.has_value());
    QCOMPARE(*plain, secret);
}

void TestTrust::dpapiTamperedFails()
{
    auto blob = protectForCurrentUser("secret");
    QVERIFY(blob.has_value());
    (*blob)[blob->size() / 2] = (char)((*blob)[blob->size() / 2] ^ 0x5A);
    QVERIFY(!unprotectForCurrentUser(*blob).has_value());
    QVERIFY(!unprotectForCurrentUser("garbage").has_value());
}
