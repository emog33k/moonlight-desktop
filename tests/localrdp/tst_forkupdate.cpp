#include "tests.h"
#include "streaming/localrdp/forkupdate.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

using LocalRdp::forkUpdateFrom;

static QByteArray release(const QString& tag, bool draft = false, bool prerelease = false,
                          const QString& url = "https://github.com/emog33k/moonlight-desktop/releases/tag/v1.1.0")
{
    return QJsonDocument(QJsonObject{ { "tag_name", tag }, { "html_url", url },
                                      { "draft", draft }, { "prerelease", prerelease } })
            .toJson(QJsonDocument::Compact);
}

void TestForkUpdate::newerReleaseOffered()
{
    const auto u = forkUpdateFrom(release("v1.1.0"), "1.0.0");
    QVERIFY(u);
    QCOMPARE(u->version, QString("1.1.0"));
    QCOMPARE(u->url, QString("https://github.com/emog33k/moonlight-desktop/releases/tag/v1.1.0"));
    // Our tags: "desktop-v1.2.0" (plain "v1.0.0" for the first release)
    const auto prefixed = forkUpdateFrom(release("desktop-v1.2.0"), "1.1.0");
    QVERIFY(prefixed);
    QCOMPARE(prefixed->version, QString("1.2.0"));
    QVERIFY(!forkUpdateFrom(release("desktop-v1.1.0"), "1.1.0"));
    QVERIFY(!forkUpdateFrom(release("other-v9.0.0"), "1.1.0"));
    // Tag without the "v", more parts than ours
    QVERIFY(forkUpdateFrom(release("1.0.0.1"), "1.0.0"));
    QVERIFY(forkUpdateFrom(release("v1.10.0"), "1.9.3"));
}

void TestForkUpdate::sameOrOlderNotOffered()
{
    QVERIFY(!forkUpdateFrom(release("v1.0.0"), "1.0.0"));
    QVERIFY(!forkUpdateFrom(release("v1.0"), "1.0.0"));
    QVERIFY(!forkUpdateFrom(release("v0.9.9"), "1.0.0"));
}

void TestForkUpdate::draftsAndPrereleasesIgnored()
{
    QVERIFY(!forkUpdateFrom(release("v2.0.0", true), "1.0.0"));
    QVERIFY(!forkUpdateFrom(release("v2.0.0", false, true), "1.0.0"));
}

void TestForkUpdate::malformedIgnored()
{
    QVERIFY(!forkUpdateFrom("not json", "1.0.0"));
    QVERIFY(!forkUpdateFrom("[]", "1.0.0"));
    QVERIFY(!forkUpdateFrom(release(""), "1.0.0"));
    QVERIFY(!forkUpdateFrom(release("latest"), "1.0.0"));
    QVERIFY(!forkUpdateFrom(release("v1.x.0"), "1.0.0"));
    // The button opens this page: only GitHub over https
    QVERIFY(!forkUpdateFrom(release("v2.0.0", false, false, "http://github.com/x"), "1.0.0"));
    QVERIFY(!forkUpdateFrom(release("v2.0.0", false, false, "https://evil.example/x"), "1.0.0"));
    QVERIFY(!forkUpdateFrom(release("v2.0.0", false, false, ""), "1.0.0"));
}
