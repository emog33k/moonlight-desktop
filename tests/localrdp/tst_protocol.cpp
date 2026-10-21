#include "tests.h"
#include "localrdp-shared/protocol.h"

#include <QJsonObject>
#include <QtEndian>
#include <QTest>

using namespace LocalRdp::Proto;

void TestProtocol::roundTrip()
{
    FrameDecoder d;
    d.feed(encodeFrame(MsgType::Hello, "{\"v\":1}"));
    const auto f = d.next();
    QVERIFY(f.has_value());
    QCOMPARE(f->type, MsgType::Hello);
    QCOMPARE(f->payload, QByteArray("{\"v\":1}"));
    QVERIFY(!d.next().has_value());
    QVERIFY(!d.failed());
}

void TestProtocol::emptyPayload()
{
    FrameDecoder d;
    d.feed(encodeFrame(MsgType::Ping, QByteArray()));
    const auto f = d.next();
    QVERIFY(f.has_value());
    QCOMPARE(f->type, MsgType::Ping);
    QVERIFY(f->payload.isEmpty());
}

void TestProtocol::splitAcrossFeeds()
{
    const QByteArray bytes = encodeFrame(MsgType::ClipData, QByteArray(1000, 'x'));
    FrameDecoder d;
    for (int i = 0; i < bytes.size(); i++) {
        QVERIFY(!d.next().has_value());
        d.feed(bytes.mid(i, 1));
    }
    const auto f = d.next();
    QVERIFY(f.has_value());
    QCOMPARE(f->payload.size(), 1000);
}

void TestProtocol::twoFramesInOneFeed()
{
    FrameDecoder d;
    d.feed(encodeFrame(MsgType::Ping, "a") + encodeFrame(MsgType::Pong, "bc"));
    QCOMPARE(d.next()->type, MsgType::Ping);
    const auto second = d.next();
    QCOMPARE(second->type, MsgType::Pong);
    QCOMPARE(second->payload, QByteArray("bc"));
    QVERIFY(!d.next().has_value());
}

void TestProtocol::oversizeFails()
{
    QByteArray header(4, 0);
    qToBigEndian<quint32>(kMaxFrame + 2, header.data());
    FrameDecoder d;
    d.feed(header);
    QVERIFY(!d.next().has_value());
    QVERIFY(d.failed());
}

void TestProtocol::zeroLengthFails()
{
    FrameDecoder d;
    d.feed(QByteArray(4, 0));
    QVERIFY(!d.next().has_value());
    QVERIFY(d.failed());
}

void TestProtocol::badJsonRejected()
{
    QVERIFY(!parseJsonPayload("{").has_value());
    QVERIFY(!parseJsonPayload("[1,2]").has_value()); // must be an object
    const auto ok = parseJsonPayload(jsonPayload(QJsonObject{ { "a", 1 } }));
    QVERIFY(ok.has_value());
    QCOMPARE(ok->value("a").toInt(), 1);
}
