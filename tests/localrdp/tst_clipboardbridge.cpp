#include "tests.h"
#include "localrdp-shared/clip/clipboardbridge_win32.h"

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
    QVERIFY(!out.waitFor(MsgType::ClipOffer, 500).has_value());
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
