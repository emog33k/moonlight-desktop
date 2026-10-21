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

void TestClipExchange::oldRequestAfterNewOfferFails()
{
    // A paste of the old clipboard must never receive the new clipboard's data
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
    QVERIFY(!pipe.fetcher.fetchRange(100, 0, 3, 3, 3000).has_value()); // old paste continues: fails
    QCOMPARE(*pipe.fetcher.fetchRange(200, 0, 0, 3, 3000), QByteArray("NEW"));
}

void TestClipExchange::moreDataThanOfferedFails()
{
    Pipe pipe;
    Offer claimed = textOffer(7, "abc");     // says 3 bytes
    pipe.source.set(claimed, QByteArray(5000, 'z'), {}, {}, {}); // sends 5000
    pipe.fetcher.setOffer(claimed);
    QVERIFY(!pipe.fetcher.fetchAll(7, What::Text, 3000).has_value());
}
