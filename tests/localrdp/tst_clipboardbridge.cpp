#include "tests.h"
#include "localrdp-shared/clip/clipboardbridge_win32.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <mutex>
#include <thread>

using namespace LocalRdp;
using namespace LocalRdp::Clip;
using Proto::MsgType;

namespace {

struct Captured {
    std::mutex mutex;
    QVector<Proto::Frame> frames;
    QVector<std::thread::id> threads;
    SendFn sender()
    {
        return [this](MsgType type, QByteArray payload) {
            std::lock_guard<std::mutex> lock(mutex);
            frames.append({ type, payload });
            threads.append(std::this_thread::get_id());
        };
    }
    std::optional<Proto::Frame> waitFor(MsgType type, int timeoutMs = 3000)
    {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < timeoutMs) {
            {
                std::lock_guard<std::mutex> lock(mutex);
                for (const auto& f : frames) {
                    if (f.type == type) return f;
                }
            }
            QThread::msleep(10);
        }
        return std::nullopt;
    }
};

}

void TestClipboardBridge::offerFilesSendsOffer()
{
    QTemporaryDir dir;
    QFile f(dir.filePath("report.pdf"));
    f.open(QIODevice::WriteOnly);
    f.write("12345");
    f.close();

    Captured out;
    ClipboardBridge bridge(out.sender(), /*useSystemClipboard*/ false);
    bridge.connectionUp();
    bridge.offerFiles({ f.fileName() });

    const auto frame = out.waitFor(MsgType::ClipOffer);
    QVERIFY(frame.has_value());
    const auto offer = decodeOffer(frame->payload);
    QVERIFY(offer.has_value());
    QCOMPARE(offer->files.size(), 1);
    QCOMPARE(offer->files[0].name, QString("report.pdf"));
    QCOMPARE(offer->files[0].size, quint64(5));
}

void TestClipboardBridge::peerRequestServedFromOfferFiles()
{
    QTemporaryDir dir;
    QFile f(dir.filePath("data.bin"));
    f.open(QIODevice::WriteOnly);
    f.write("hello from the laptop");
    f.close();

    Captured out;
    ClipboardBridge bridge(out.sender(), false);
    bridge.connectionUp();
    bridge.offerFiles({ f.fileName() });
    const auto offer = decodeOffer(out.waitFor(MsgType::ClipOffer)->payload);

    Request r;
    r.id = 77;
    r.seq = offer->seq;
    r.what = What::File;
    r.index = 0;
    r.length = 1000;
    bridge.onFrame({ MsgType::ClipRequest, encodeRequest(r) });

    const auto data = out.waitFor(MsgType::ClipData);
    QVERIFY(data.has_value());
    const auto chunk = decodeChunk(data->payload);
    QCOMPARE(chunk->id, quint32(77));
    QCOMPARE(chunk->data, QByteArray("hello from the laptop"));
}

void TestClipboardBridge::disabledSendsNothing()
{
    QTemporaryDir dir;
    QFile f(dir.filePath("x.txt"));
    f.open(QIODevice::WriteOnly);
    f.close();

    Captured out;
    ClipboardBridge bridge(out.sender(), false);
    bridge.connectionUp();
    bridge.setEnabled(false);
    bridge.offerFiles({ f.fileName() });
    QTest::qWait(500);
    // Only the empty "drop what I offered" announcement, never content
    std::lock_guard<std::mutex> lock(out.mutex);
    for (const auto& frame : out.frames) {
        QCOMPARE(frame.type, MsgType::ClipOffer);
        QVERIFY(decodeOffer(frame.payload)->isEmpty());
    }
}

void TestClipboardBridge::notConnectedSendsNothing()
{
    QTemporaryDir dir;
    QFile f(dir.filePath("x.txt"));
    f.open(QIODevice::WriteOnly);
    f.close();

    Captured out;
    ClipboardBridge bridge(out.sender(), false);
    bridge.offerFiles({ f.fileName() }); // connection not up yet
    QVERIFY(!out.waitFor(MsgType::ClipOffer, 500).has_value());
}

void TestClipboardBridge::badFramesIgnored()
{
    Captured out;
    ClipboardBridge bridge(out.sender(), false);
    bridge.connectionUp();
    bridge.onFrame({ MsgType::ClipOffer, "not json" });
    bridge.onFrame({ MsgType::ClipRequest, "{\"id\":1}" });
    bridge.onFrame({ MsgType::ClipData, "x" });
    bridge.onFrame({ MsgType::ClipError, "" });
    QTest::qWait(200);
    // A request for an offer we never made gets a polite error, nothing else
    Request r;
    r.id = 5;
    r.seq = 12345;
    bridge.onFrame({ MsgType::ClipRequest, encodeRequest(r) });
    const auto err = out.waitFor(MsgType::ClipError);
    QVERIFY(err.has_value());
    QCOMPARE(decodeError(err->payload)->first, quint32(5));
}

void TestClipboardBridge::systemClipboardRoundTrip()
{
    if (!qEnvironmentVariableIsSet("LOCALRDP_CLIPBOARD_TESTS")) {
        QSKIP("Set LOCALRDP_CLIPBOARD_TESTS=1 to run (it uses the real clipboard)");
    }
    // Two bridges wired to each other; A's clipboard change shows up on B's side.
    Captured toB;
    ClipboardBridge a(toB.sender(), true);
    a.connectionUp();
    // Put text on the real clipboard
    if (OpenClipboard(nullptr)) {
        EmptyClipboard();
        const wchar_t text[] = L"LocalRDP clipboard test";
        HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, sizeof(text));
        memcpy(GlobalLock(h), text, sizeof(text));
        GlobalUnlock(h);
        SetClipboardData(CF_UNICODETEXT, h);
        CloseClipboard();
    }
    const auto offer = toB.waitFor(MsgType::ClipOffer, 5000);
    QVERIFY(offer.has_value());
    QCOMPARE(*decodeOffer(offer->payload)->textBytes, quint64(sizeof(L"LocalRDP clipboard test") - 2));
}

void TestClipboardBridge::fileRequestsServedOffCallerThread()
{
    // Disk reads (OneDrive placeholders can take seconds) must not run on the
    // network thread that delivers the request
    QTemporaryDir dir;
    QFile f(dir.filePath("d.bin"));
    f.open(QIODevice::WriteOnly);
    f.write("data");
    f.close();

    Captured out;
    ClipboardBridge bridge(out.sender(), false);
    bridge.connectionUp();
    bridge.offerFiles({ f.fileName() });
    const auto offer = decodeOffer(out.waitFor(MsgType::ClipOffer)->payload);

    Request r;
    r.id = 1;
    r.seq = offer->seq;
    r.what = What::File;
    r.length = 100;
    bridge.onFrame({ MsgType::ClipRequest, encodeRequest(r) });
    QVERIFY(out.waitFor(MsgType::ClipData).has_value());
    std::lock_guard<std::mutex> lock(out.mutex);
    for (int i = 0; i < out.frames.size(); i++) {
        if (out.frames[i].type == MsgType::ClipData) {
            QVERIFY(out.threads[i] != std::this_thread::get_id());
        }
    }
}

void TestClipboardBridge::offerFilesReportsOutcome()
{
    QTemporaryDir dir;
    QFile f(dir.filePath("ok.txt"));
    f.open(QIODevice::WriteOnly);
    f.close();

    Captured out;
    ClipboardBridge bridge(out.sender(), false);
    bridge.connectionUp();

    std::atomic<int> result { -1 };
    QString reason;
    std::mutex m;
    bridge.offerFiles({ dir.filePath("does-not-exist.txt") }, [&](bool ok, QString why) {
        std::lock_guard<std::mutex> lock(m);
        reason = why;
        result = ok ? 1 : 0;
    });
    QTRY_VERIFY_WITH_TIMEOUT(result.load() != -1, 3000);
    QCOMPARE(result.load(), 0);
    {
        std::lock_guard<std::mutex> lock(m);
        QVERIFY(!reason.isEmpty());
    }

    result = -1;
    bridge.offerFiles({ f.fileName() }, [&](bool ok, QString) { result = ok ? 1 : 0; });
    QTRY_VERIFY_WITH_TIMEOUT(result.load() != -1, 3000);
    QCOMPARE(result.load(), 1);
}

void TestClipboardBridge::seqStartsRandom()
{
    // After a restart an old paste must not match a fresh offer by accident
    QTemporaryDir dir;
    QFile f(dir.filePath("s.txt"));
    f.open(QIODevice::WriteOnly);
    f.close();
    quint64 seqs[2];
    for (int i = 0; i < 2; i++) {
        Captured out;
        ClipboardBridge bridge(out.sender(), false);
        bridge.connectionUp();
        bridge.offerFiles({ f.fileName() });
        seqs[i] = decodeOffer(out.waitFor(MsgType::ClipOffer)->payload)->seq;
    }
    QVERIFY(seqs[0] != seqs[1]);
    QVERIFY(seqs[0] > 1000 && seqs[1] > 1000);
}

namespace {
Proto::Frame offerFrame(quint64 seq, qint64 copiedAt, bool initial, bool empty = false)
{
    Offer o;
    o.seq = seq;
    if (!empty) {
        o.textBytes = 4;
    }
    o.copiedAt = copiedAt;
    o.initial = initial;
    return { MsgType::ClipOffer, encodeOffer(o) };
}
}

void TestClipboardBridge::initialOfferNewerSideWins()
{
    Captured out;
    ClipboardBridge bridge(out.sender(), false);
    std::mutex m;
    QVector<quint64> applied;
    bridge.setPeerOfferObserver([&](const Offer& o) { std::lock_guard<std::mutex> l(m); applied << o.seq; });
    bridge.setLocalCopyTime(1000);
    bridge.connectionUp();

    bridge.onFrame(offerFrame(1, 500, true));        // peer's clipboard is older: keep ours
    bridge.onFrame(offerFrame(2, 0, true, true));    // nothing on the peer: keep ours
    bridge.onFrame(offerFrame(3, 500, false));       // a fresh copy on the peer: always taken
    bridge.onFrame(offerFrame(4, 2000, true));       // peer's is newer: taken
    QTest::qWait(300);
    std::lock_guard<std::mutex> l(m);
    QCOMPARE(applied, QVector<quint64>({ 3, 4 }));
}

void TestClipboardBridge::sentOffersCarryCopyTime()
{
    QTemporaryDir dir;
    QFile f(dir.filePath("t.txt"));
    f.open(QIODevice::WriteOnly);
    f.close();
    Captured out;
    ClipboardBridge bridge(out.sender(), false);
    bridge.connectionUp();
    const qint64 before = QDateTime::currentMSecsSinceEpoch();
    bridge.offerFiles({ f.fileName() });
    const auto offer = decodeOffer(out.waitFor(MsgType::ClipOffer)->payload);
    QVERIFY(offer->copiedAt >= before && offer->copiedAt <= QDateTime::currentMSecsSinceEpoch());
    QCOMPARE(offer->initial, false);
}

void TestClipboardBridge::offerAfterDisconnectIgnored()
{
    Captured out;
    ClipboardBridge bridge(out.sender(), false);
    std::atomic<int> applied { 0 };
    bridge.setPeerOfferObserver([&](const Offer&) { applied++; });
    bridge.connectionUp();
    bridge.connectionDown();
    bridge.onFrame(offerFrame(1, 5000, false)); // late frame from the closed connection
    bridge.setEnabled(false);
    bridge.connectionUp();
    bridge.onFrame(offerFrame(2, 5000, false)); // sync turned off
    QTest::qWait(300);
    QCOMPARE(applied.load(), 0);
}

void TestClipboardBridge::disablingSendsEmptyOfferAndStopsServing()
{
    QTemporaryDir dir;
    QFile f(dir.filePath("d.bin"));
    f.open(QIODevice::WriteOnly);
    f.write("secret");
    f.close();
    Captured out;
    ClipboardBridge bridge(out.sender(), false);
    bridge.connectionUp();
    bridge.offerFiles({ f.fileName() });
    const auto offer = decodeOffer(out.waitFor(MsgType::ClipOffer)->payload);

    bridge.setEnabled(false);
    QTest::qWait(300);
    {
        std::lock_guard<std::mutex> lock(out.mutex);
        QVERIFY(out.frames.size() >= 2);
        const auto last = decodeOffer(out.frames.last().payload);
        QCOMPARE(out.frames.last().type, MsgType::ClipOffer);
        QVERIFY(last->isEmpty()); // the peer drops what we had offered
        out.frames.clear();
    }
    Request r;
    r.id = 9;
    r.seq = offer->seq;
    r.what = What::File;
    r.length = 100;
    bridge.onFrame({ MsgType::ClipRequest, encodeRequest(r) });
    QVERIFY(out.waitFor(MsgType::ClipError).has_value());
    QVERIFY(!out.waitFor(MsgType::ClipData, 300).has_value());
}

void TestClipboardBridge::disableStopsServingAtOnce()
{
    // The PC locks while its clipboard thread is busy (e.g. a slow paste): the
    // clipboard must stop being served right away, not when that thread is free
    QTemporaryDir dir;
    QFile f(dir.filePath("s.bin"));
    f.open(QIODevice::WriteOnly);
    f.write("secret");
    f.close();
    Captured out;
    ClipboardBridge bridge(out.sender(), false);
    bridge.connectionUp();
    std::mutex m;
    std::condition_variable cv;
    bool release = false, inside = false;
    bridge.offerFiles({ f.fileName() }, [&](bool, QString) {
        std::unique_lock<std::mutex> lock(m);
        inside = true;
        cv.notify_all();
        cv.wait(lock, [&]() { return release; }); // keeps the clipboard thread busy
    });
    {
        std::unique_lock<std::mutex> lock(m);
        cv.wait(lock, [&]() { return inside; });
    }
    const auto offer = decodeOffer(out.waitFor(MsgType::ClipOffer)->payload);

    bridge.setEnabled(false);
    Request r;
    r.id = 4;
    r.seq = offer->seq;
    r.what = What::File;
    r.length = 100;
    bridge.onFrame({ MsgType::ClipRequest, encodeRequest(r) });
    QTest::qWait(300);
    bool servedData = false;
    {
        std::lock_guard<std::mutex> lock(out.mutex);
        for (const auto& fr : out.frames) servedData |= fr.type == MsgType::ClipData;
    }
    {
        std::lock_guard<std::mutex> lock(m);
        release = true;
    }
    cv.notify_all();
    QVERIFY(!servedData);
}
