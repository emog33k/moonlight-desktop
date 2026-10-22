#include "tests.h"
#include "localrdp-shared/clip/clipexchange.h"

#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QTest>

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

using namespace LocalRdp;
using namespace LocalRdp::Clip;
using Proto::MsgType;

namespace {

// Emulates the connection: frames are delivered on a separate "network" thread.
class Pipe
{
public:
    LocalClipSource source;   // the side that copied
    RemoteFetcher fetcher;    // the side that pastes
    bool dropRequests = false;

    Pipe()
        : fetcher([this](MsgType type, QByteArray payload) { post([this, type, payload]() { toSource(type, payload); }); })
    {
        m_Thread = std::thread([this]() { run(); });
    }
    ~Pipe()
    {
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            m_Stop = true;
        }
        m_Cv.notify_all();
        m_Thread.join();
    }

private:
    void toSource(MsgType type, const QByteArray& payload)
    {
        if (type != MsgType::ClipRequest || dropRequests) {
            return;
        }
        const auto req = decodeRequest(payload);
        source.serve(*req, [this](MsgType replyType, QByteArray reply) {
            post([this, replyType, reply]() {
                if (replyType == MsgType::ClipData) fetcher.onData(reply);
                else if (replyType == MsgType::ClipError) fetcher.onError(reply);
            });
        });
    }
    void post(std::function<void()> f)
    {
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            m_Queue.push_back(std::move(f));
        }
        m_Cv.notify_all();
    }
    void run()
    {
        for (;;) {
            std::function<void()> f;
            {
                std::unique_lock<std::mutex> lock(m_Mutex);
                m_Cv.wait(lock, [this]() { return m_Stop || !m_Queue.empty(); });
                if (m_Stop) return;
                f = std::move(m_Queue.front());
                m_Queue.pop_front();
            }
            f();
        }
    }

    std::mutex m_Mutex;
    std::condition_variable m_Cv;
    std::deque<std::function<void()>> m_Queue;
    bool m_Stop = false;
    std::thread m_Thread;
};

Offer textOffer(quint64 seq, const QByteArray& text)
{
    Offer o;
    o.seq = seq;
    o.textBytes = text.size();
    return o;
}

}

void TestClipExchange::textRoundTrip()
{
    Pipe pipe;
    QByteArray text(3 * 1024 * 1024 + 17, 'x');
    for (int i = 0; i < text.size(); i++) text[i] = char('a' + i % 26);
    const Offer offer = textOffer(5, text);
    pipe.source.set(offer, text, {}, {}, {});
    pipe.fetcher.setOffer(offer);

    const auto got = pipe.fetcher.fetchAll(5, What::Text);
    QVERIFY(got.has_value());
    QCOMPARE(got->size(), text.size());
    QVERIFY(*got == text);
}

void TestClipExchange::fileRangeFromDisk()
{
    QTemporaryDir dir;
    const QString path = dir.filePath("big.bin");
    QByteArray content(5 * 1024 * 1024, 0);
    for (int i = 0; i < content.size(); i++) content[i] = char(i * 7);
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write(content);
    f.close();

    Offer offer;
    offer.seq = 9;
    offer.files = { { "folder", 0, true }, { "folder\\big.bin", (quint64)content.size(), false } };
    Pipe pipe;
    pipe.source.set(offer, {}, {}, {}, { QString(), path });
    pipe.fetcher.setOffer(offer);

    const auto part = pipe.fetcher.fetchRange(9, 1, 2 * 1024 * 1024 + 3, 100000);
    QVERIFY(part.has_value());
    QCOMPARE(part->size(), 100000);
    QVERIFY(*part == content.mid(2 * 1024 * 1024 + 3, 100000));

    const auto tail = pipe.fetcher.fetchRange(9, 1, content.size() - 10, kClipChunk);
    QCOMPARE(tail->size(), 10); // short read at end of file
}

void TestClipExchange::staleSeqErrors()
{
    Pipe pipe;
    pipe.source.set(textOffer(2, "new"), "new", {}, {}, {});
    pipe.fetcher.setOffer(textOffer(1, "old")); // fetcher still thinks seq 1
    QVERIFY(!pipe.fetcher.fetchAll(1, What::Text, 3000).has_value());
}

void TestClipExchange::newOfferCancelsWait()
{
    Pipe pipe;
    pipe.dropRequests = true; // nobody answers
    pipe.fetcher.setOffer(textOffer(1, "x"));
    std::thread later([&pipe]() {
        QThread::msleep(100);
        pipe.fetcher.setOffer(textOffer(2, "y"));
    });
    QElapsedTimer t;
    t.start();
    QVERIFY(!pipe.fetcher.fetchAll(1, What::Text, 10000).has_value());
    QVERIFY2(t.elapsed() < 1000, qPrintable(QString::number(t.elapsed())));
    later.join();
}

void TestClipExchange::disconnectCancelsWait()
{
    Pipe pipe;
    pipe.dropRequests = true;
    pipe.fetcher.setOffer(textOffer(1, "x"));
    std::thread later([&pipe]() {
        QThread::msleep(100);
        pipe.fetcher.disconnected();
    });
    QElapsedTimer t;
    t.start();
    QVERIFY(!pipe.fetcher.fetchAll(1, What::Text, 10000).has_value());
    QVERIFY(t.elapsed() < 1000);
    later.join();
}

void TestClipExchange::timeoutReturnsEmpty()
{
    Pipe pipe;
    pipe.dropRequests = true;
    pipe.fetcher.setOffer(textOffer(1, "x"));
    QElapsedTimer t;
    t.start();
    QVERIFY(!pipe.fetcher.fetchAll(1, What::Text, 200).has_value());
    QVERIFY(t.elapsed() >= 150 && t.elapsed() < 2000);
}

void TestClipExchange::directoryIndexRejected()
{
    Offer offer;
    offer.seq = 3;
    offer.files = { { "dir", 0, true } };
    Pipe pipe;
    pipe.source.set(offer, {}, {}, {}, { QString() });
    pipe.fetcher.setOffer(offer);
    QVERIFY(!pipe.fetcher.fetchRange(3, 0, 0, 10, 3000).has_value());
}

void TestClipExchange::indexOutOfRangeRejected()
{
    QTemporaryDir dir;
    QFile f(dir.filePath("a.txt"));
    f.open(QIODevice::WriteOnly);
    f.write("hello");
    f.close();
    Offer offer;
    offer.seq = 3;
    offer.files = { { "a.txt", 5, false } };
    Pipe pipe;
    pipe.source.set(offer, {}, {}, {}, { f.fileName() });
    pipe.fetcher.setOffer(offer);
    QVERIFY(!pipe.fetcher.fetchRange(3, 1, 0, 10, 3000).has_value());
    QVERIFY(!pipe.fetcher.fetchRange(3, -1, 0, 10, 3000).has_value());
    QCOMPARE(*pipe.fetcher.fetchRange(3, 0, 0, 10, 3000), QByteArray("hello"));
}

void TestClipExchange::clearedSourceErrors()
{
    Pipe pipe;
    pipe.source.set(textOffer(4, "abc"), "abc", {}, {}, {});
    pipe.fetcher.setOffer(textOffer(4, "abc"));
    pipe.source.clear();
    QVERIFY(!pipe.fetcher.fetchAll(4, What::Text, 3000).has_value());
}

void TestClipExchange::oldFilePasteContinuesAfterNewOffer()
{
    // A long file paste keeps going when the source copies something else,
    // and never receives the new clipboard's data.
    QTemporaryDir dir;
    QFile a(dir.filePath("a.bin"));
    a.open(QIODevice::WriteOnly);
    a.write("OLD-CONTENT");
    a.close();
    QFile b(dir.filePath("b.bin"));
    b.open(QIODevice::WriteOnly);
    b.write("NEW-CONTENT");
    b.close();

    Offer first;
    first.seq = 100;
    first.files = { { "x.bin", 11, false } };
    Offer second = first;
    second.seq = 200;

    Pipe pipe;
    pipe.source.set(first, {}, {}, {}, { a.fileName() });
    pipe.fetcher.setOffer(first);
    QCOMPARE(*pipe.fetcher.fetchRange(100, 0, 0, 3, 3000), QByteArray("OLD"));

    pipe.source.set(second, {}, {}, {}, { b.fileName() });
    pipe.fetcher.setOffer(second);
    QCOMPARE(*pipe.fetcher.fetchRange(100, 0, 3, 4, 3000), QByteArray("-CON")); // still the old file
    QCOMPARE(*pipe.fetcher.fetchRange(200, 0, 0, 3, 3000), QByteArray("NEW"));
    // Text of an old offer is not kept
    pipe.source.set(textOffer(300, "t"), "t", {}, {}, {});
    pipe.fetcher.setOffer(textOffer(300, "t"));
    QVERIFY(!pipe.fetcher.fetchAll(299, What::Text, 1000).has_value());
}

void TestClipExchange::olderFileOffersExpire()
{
    QTemporaryDir dir;
    QFile a(dir.filePath("a.bin"));
    a.open(QIODevice::WriteOnly);
    a.write("AAAA");
    a.close();
    Pipe pipe;
    for (quint64 seq = 1; seq <= 6; seq++) {
        Offer o;
        o.seq = seq;
        o.files = { { "a.bin", 4, false } };
        pipe.source.set(o, {}, {}, {}, { a.fileName() });
        pipe.fetcher.setOffer(o);
    }
    QVERIFY(pipe.fetcher.fetchRange(6, 0, 0, 4, 3000).has_value());
    QVERIFY(pipe.fetcher.fetchRange(3, 0, 0, 4, 3000).has_value()); // a few back: still there
    QVERIFY(!pipe.fetcher.fetchRange(1, 0, 0, 4, 3000).has_value()); // long ago: gone
    pipe.source.clear();
    QVERIFY(!pipe.fetcher.fetchRange(6, 0, 0, 4, 3000).has_value()); // cleared: nothing at all
}

namespace {
// A fetcher whose requests are only recorded; the test answers by hand.
struct ManualPeer {
    std::mutex mutex;
    QVector<Request> requests;
    RemoteFetcher fetcher { [this](MsgType type, QByteArray payload) {
        if (type == MsgType::ClipRequest) {
            std::lock_guard<std::mutex> lock(mutex);
            requests << *decodeRequest(payload);
        }
    } };
    std::optional<Request> waitRequest()
    {
        for (int i = 0; i < 300; i++) {
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (!requests.isEmpty()) return requests.last();
            }
            QThread::msleep(10);
        }
        return std::nullopt;
    }
};
Offer fileOffer(quint64 seq)
{
    Offer o;
    o.seq = seq;
    o.files = { { "f.bin", 100, false } };
    return o;
}
}

void TestClipExchange::pendingFileFetchSurvivesNewOffer()
{
    ManualPeer peer;
    peer.fetcher.setOffer(fileOffer(10));
    std::optional<QByteArray> got;
    std::thread paste([&]() { got = peer.fetcher.fetchRange(10, 0, 0, 5, 3000); });
    const auto request = peer.waitRequest();
    QVERIFY(request.has_value());
    peer.fetcher.setOffer(fileOffer(11)); // the PC copied something new meanwhile
    peer.fetcher.onData(encodeChunk({ request->id, true, "12345" }));
    paste.join();
    QVERIFY(got.has_value());
    QCOMPARE(*got, QByteArray("12345"));
}

void TestClipExchange::heartbeatKeepsSlowFileAlive()
{
    // The source is still reading (e.g. a OneDrive file downloading): it sends
    // empty "still working" chunks; the wait must not time out meanwhile.
    ManualPeer peer;
    peer.fetcher.setOffer(fileOffer(20));
    std::optional<QByteArray> got;
    std::thread paste([&]() { got = peer.fetcher.fetchRange(20, 0, 0, 3, 300); });
    const auto request = peer.waitRequest();
    QVERIFY(request.has_value());
    for (int i = 0; i < 8; i++) {
        QThread::msleep(100);
        peer.fetcher.onData(encodeChunk({ request->id, false, QByteArray() }));
    }
    peer.fetcher.onData(encodeChunk({ request->id, true, "abc" }));
    paste.join();
    QVERIFY(got.has_value());
    QCOMPARE(*got, QByteArray("abc"));
}

void TestClipExchange::moreDataThanOfferedFails()
{
    Pipe pipe;
    Offer claimed = textOffer(7, "abc");     // says 3 bytes
    pipe.source.set(claimed, QByteArray(5000, 'z'), {}, {}, {}); // sends 5000
    pipe.fetcher.setOffer(claimed);
    QVERIFY(!pipe.fetcher.fetchAll(7, What::Text, 3000).has_value());
}

void TestClipExchange::requestIdsStartRandom()
{
    // A new connection's requests never reuse the ids a previous one had in flight
    ManualPeer a, b;
    a.fetcher.setOffer(textOffer(1, "x"));
    b.fetcher.setOffer(textOffer(1, "x"));
    std::thread ta([&]() { a.fetcher.fetchAll(1, What::Text, 200); });
    std::thread tb([&]() { b.fetcher.fetchAll(1, What::Text, 200); });
    const auto ra = a.waitRequest();
    const auto rb = b.waitRequest();
    ta.join();
    tb.join();
    QVERIFY(ra && rb);
    QVERIFY(ra->id != rb->id);
    QVERIFY(ra->id > 1000);
}
