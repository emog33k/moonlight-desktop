#include "tests.h"
#include "localrdp-shared/hostdisplays.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

using namespace LocalRdp::Proto;

static DisplayList twoDisplays()
{
    DisplayList l;
    l.displays = { { "\\\\.\\DISPLAY1", 2560, 1440, true }, { "\\\\.\\DISPLAY2", 1920, 1080, false } };
    l.initial = 1;
    return l;
}

static QByteArray json(const QJsonObject& o)
{
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

static QJsonObject entry(const QString& name, int w, int h)
{
    return QJsonObject{ { "name", name }, { "width", w }, { "height", h }, { "primary", false } };
}

void TestHostDisplays::roundTrip()
{
    const auto decoded = decodeDisplayList(encodeDisplayList(twoDisplays()));
    QVERIFY(decoded);
    QVERIFY(*decoded == twoDisplays());
}

void TestHostDisplays::emptyList()
{
    const auto decoded = decodeDisplayList(encodeDisplayList(DisplayList()));
    QVERIFY(decoded);
    QVERIFY(decoded->displays.isEmpty());
    QCOMPARE(decoded->initial, -1);
}

void TestHostDisplays::malformedRejected()
{
    QVERIFY(!decodeDisplayList("not json"));
    QVERIFY(!decodeDisplayList(json(QJsonObject{ { "displays", "x" } })));
    QVERIFY(!decodeDisplayList(json(QJsonObject{ { "displays", QJsonArray{ 5 } } })));
    QVERIFY(!decodeDisplayList(json(QJsonObject{ { "displays", QJsonArray{ entry("a", 0, 1080) } } })));
    QVERIFY(!decodeDisplayList(json(QJsonObject{ { "displays", QJsonArray{ entry("a", 1920, 100000) } } })));
    QVERIFY(!decodeDisplayList(json(QJsonObject{ { "displays", QJsonArray{ entry(QString(65, 'x'), 1920, 1080) } } })));
    QJsonArray many;
    for (int i = 0; i < kMaxDisplays + 1; i++) {
        many << entry("d", 800, 600);
    }
    QVERIFY(!decodeDisplayList(json(QJsonObject{ { "displays", many } })));
}

void TestHostDisplays::initialOutOfRangeUnknown()
{
    DisplayList l = twoDisplays();
    l.initial = 2;
    QCOMPARE(decodeDisplayList(encodeDisplayList(l))->initial, -1);
    l.initial = -7;
    QCOMPARE(decodeDisplayList(encodeDisplayList(l))->initial, -1);
}

void TestHostDisplays::initialFromConfig()
{
    const QVector<HostDisplay> d = twoDisplays().displays;
    QCOMPARE(initialDisplayFromConfig(d, ""), 0);
    QCOMPARE(initialDisplayFromConfig(d, "sunshine_name = PC\r\n"), 0);
    QCOMPARE(initialDisplayFromConfig(d, "output_name = \\\\.\\DISPLAY2\r\n"), 1);
    QCOMPARE(initialDisplayFromConfig(d, "  output_name=\\\\.\\display2  \n"), 1);
    QCOMPARE(initialDisplayFromConfig(d, "# output_name = \\\\.\\DISPLAY2\n"), 0);
    QCOMPARE(initialDisplayFromConfig(d, "output_name = \n"), 0);
    QCOMPARE(initialDisplayFromConfig(d, "output_name = {6f1e9c3a-0000-1111-2222-333344445555}\n"), -1);
    QCOMPARE(initialDisplayFromConfig(d, "output_name = \\\\.\\DISPLAY9\n"), -1);
    QCOMPARE(initialDisplayFromConfig({}, ""), -1);
}
