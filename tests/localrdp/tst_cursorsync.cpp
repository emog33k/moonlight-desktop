#include "tests.h"
#include "streaming/localrdp/cursorsync.h"

#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

using namespace LocalRdp;
using namespace LocalRdp::Proto;

namespace {

class FakeCursorHost : public CursorSync::Host
{
public:
    int toggles = 0;
    int restores = 0;
    QVector<CursorKind> shown;
    void toggleSunshineCursor() override { toggles++; }
    void showPcCursor(const CursorState& c) override { shown << c.kind; }
    void restoreLocalCursor() override { restores++; }
};

CursorState hidden()
{
    CursorState c;
    c.kind = CursorKind::Hidden;
    return c;
}

}

static QTemporaryDir* s_CursorDir;

void TestCursorSync::init()
{
    s_CursorDir = new QTemporaryDir();
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, s_CursorDir->path());
}

void TestCursorSync::cleanup()
{
    delete s_CursorDir;
    s_CursorDir = nullptr;
}

void TestCursorSync::activeHidesSunshineCursorOnce()
{
    FakeCursorHost host;
    CursorSync sync(&host);
    sync.update(true, true, true, "1234");
    QVERIFY(sync.isActive());
    QCOMPARE(host.toggles, 1);
    QCOMPARE(host.shown, QVector<CursorKind>({ CursorKind::Default })); // arrow until the PC reports its cursor
    sync.update(true, true, true, "1234");
    QCOMPARE(host.toggles, 1);
}

void TestCursorSync::rememberedAcrossStreams()
{
    {
        FakeCursorHost host;
        CursorSync sync(&host);
        sync.update(true, true, true, "1234");
        QCOMPARE(host.toggles, 1);
    }
    // Next stream, same Sunshine run: its cursor is still hidden
    FakeCursorHost host;
    CursorSync sync(&host);
    sync.update(true, true, true, "1234");
    QCOMPARE(host.toggles, 0);
    QVERIFY(sync.isActive());
}

void TestCursorSync::sunshineRestartStartsShown()
{
    {
        FakeCursorHost host;
        CursorSync sync(&host);
        sync.update(true, true, true, "1234");
    }
    // Sunshine restarted (new process): its cursor is shown again, hide it again
    FakeCursorHost host;
    CursorSync sync(&host);
    sync.update(true, true, true, "5678");
    QCOMPARE(host.toggles, 1);
}

void TestCursorSync::gameModeShowsSunshineCursor()
{
    FakeCursorHost host;
    CursorSync sync(&host);
    sync.update(true, true, true, "1234");
    sync.update(true, false, true, "1234");
    QVERIFY(!sync.isActive());
    QCOMPARE(host.toggles, 2);
    QCOMPARE(host.restores, 1);
    // Back to desktop mode
    sync.update(true, true, true, "1234");
    QCOMPARE(host.toggles, 3);
}

void TestCursorSync::agentLostKeepsLocalArrow()
{
    FakeCursorHost host;
    CursorSync sync(&host);
    sync.update(true, true, true, "1234");
    // The agent dropped (often because the whole connection is going down): no
    // key is sent into a stream that may be dead; the laptop shows a plain arrow
    sync.update(true, true, false, QString());
    QVERIFY(sync.isActive());
    QCOMPARE(host.toggles, 1);
    QCOMPARE(host.shown.last(), CursorKind::Default);
    // Back: the PC's cursor again
    CursorState hiddenCursor;
    hiddenCursor.kind = CursorKind::Hidden;
    sync.setPcCursor(hiddenCursor);
    sync.update(true, true, true, "1234");
    QCOMPARE(host.toggles, 1);
    QCOMPARE(host.shown.last(), CursorKind::Hidden);
    // The user switches to game mode: Sunshine's cursor is needed again
    sync.update(true, false, false, QString());
    QCOMPARE(host.toggles, 2);
    QCOMPARE(host.restores, 1);
}

void TestCursorSync::recordsArePerPc()
{
    {
        FakeCursorHost host;
        CursorSync sync(&host, "pc-a");
        sync.update(true, true, true, "1");
        QCOMPARE(host.toggles, 1);
    }
    {
        FakeCursorHost host;
        CursorSync sync(&host, "pc-b");
        sync.update(true, true, true, "2");
        QCOMPARE(host.toggles, 1);
    }
    // Back to the first PC, same Sunshine run: still hidden there
    FakeCursorHost host;
    CursorSync sync(&host, "pc-a");
    sync.update(true, true, true, "1");
    QCOMPARE(host.toggles, 0);
}

void TestCursorSync::notStartedWithoutAgent()
{
    FakeCursorHost host;
    CursorSync sync(&host);
    sync.update(true, true, false, QString());
    QVERIFY(!sync.isActive());
    QCOMPARE(host.toggles, 0);
    QVERIFY(host.shown.isEmpty());
}

void TestCursorSync::settingOffNeverToggles()
{
    FakeCursorHost host;
    CursorSync sync(&host);
    sync.update(false, true, true, "1234");
    QVERIFY(!sync.isActive());
    QCOMPARE(host.toggles, 0);
    QVERIFY(host.shown.isEmpty());
}

void TestCursorSync::unknownSunshineNoToggle()
{
    FakeCursorHost host;
    CursorSync sync(&host);
    sync.update(true, true, true, QString());
    QVERIFY(!sync.isActive());
    QCOMPARE(host.toggles, 0);
}

void TestCursorSync::pcCursorForwardedWhileActive()
{
    FakeCursorHost host;
    CursorSync sync(&host);
    sync.setPcCursor(hidden()); // not active yet: kept for later
    QVERIFY(host.shown.isEmpty());
    sync.update(true, true, true, "1234");
    QCOMPARE(host.shown, QVector<CursorKind>({ CursorKind::Hidden }));
    CursorState arrow;
    sync.setPcCursor(arrow);
    QCOMPARE(host.shown, QVector<CursorKind>({ CursorKind::Hidden, CursorKind::Default }));
    sync.update(false, true, true, "1234");
    sync.setPcCursor(hidden());
    QCOMPARE(host.shown.size(), 2);
}
