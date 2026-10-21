#include "tests.h"
#include "localrdp-shared/clip/clipmessages.h"

#include <QTest>

using namespace LocalRdp::Clip;

void TestClipMessages::offerRoundTrip()
{
    Offer o;
    o.seq = 42;
    o.textBytes = 10;
    o.imageBytes = 1000;
    o.pngBytes = 500;
    o.files = { { "папка", 0, true }, { "папка\\файл.png", 12345, false } };

    const auto back = decodeOffer(encodeOffer(o));
    QVERIFY(back.has_value());
    QCOMPARE(back->seq, quint64(42));
    QCOMPARE(*back->textBytes, quint64(10));
    QCOMPARE(*back->imageBytes, quint64(1000));
    QCOMPARE(*back->pngBytes, quint64(500));
    QCOMPARE(back->files.size(), 2);
    QCOMPARE(back->files[1].name, QString("папка\\файл.png"));
    QCOMPARE(back->files[1].size, quint64(12345));
    QCOMPARE(back->files[0].isDir, true);
}

void TestClipMessages::emptyOffer()
{
    Offer o;
    o.seq = 1;
    QVERIFY(o.isEmpty());
    const auto back = decodeOffer(encodeOffer(o));
    QVERIFY(back.has_value());
    QVERIFY(back->isEmpty());
    QVERIFY(!back->textBytes.has_value());
}

void TestClipMessages::unsafeNamesRejected()
{
    const QStringList bad = {
        "..\\x", "a\\..\\b", "..", "C:\\x", "c:x", "\\x", "/x", "\\\\srv\\x", "a<b", "a>b", "a|b",
        "a?b", "a*b", "a\"b", QString("a") + QChar(0) + "b", QString("a") + QChar(7), QString(300, 'a'), "",
        "a/../b",
    };
    for (const QString& name : bad) {
        QVERIFY2(!isSafeRelativeName(name), qPrintable(name));
        Offer o;
        o.seq = 1;
        o.files = { { name, 1, false } };
        QVERIFY2(!decodeOffer(encodeOffer(o)).has_value(), qPrintable(name));
    }
}

void TestClipMessages::safeNames()
{
    for (const QString& name : QStringList{ "a.txt", "папка\\файл.png", "dir\\sub", "a b (1).doc", "...txt" }) {
        QVERIFY2(isSafeRelativeName(name), qPrintable(name));
    }
}

void TestClipMessages::limits()
{
    Offer many;
    many.seq = 1;
    for (int i = 0; i < 10001; i++) {
        many.files.append({ QString("f%1").arg(i), 1, false });
    }
    QVERIFY(!decodeOffer(encodeOffer(many)).has_value());

    Offer huge;
    huge.seq = 1;
    huge.files = { { "a", 40ull << 30, false }, { "b", 30ull << 30, false } }; // 70 GiB
    QVERIFY(!decodeOffer(encodeOffer(huge)).has_value());

    Offer bigText;
    bigText.seq = 1;
    bigText.textBytes = (64ull << 20) + 2;
    QVERIFY(!decodeOffer(encodeOffer(bigText)).has_value());

    Offer bigImage;
    bigImage.seq = 1;
    bigImage.imageBytes = (256ull << 20) + 1;
    QVERIFY(!decodeOffer(encodeOffer(bigImage)).has_value());
}

void TestClipMessages::requestRoundTrip()
{
    Request r;
    r.id = 7;
    r.seq = 99;
    r.what = What::File;
    r.index = 3;
    r.offset = 5ull << 30;
    r.length = 65536;
    const auto back = decodeRequest(encodeRequest(r));
    QVERIFY(back.has_value());
    QCOMPARE(back->id, quint32(7));
    QCOMPARE(back->seq, quint64(99));
    QCOMPARE(back->what, What::File);
    QCOMPARE(back->index, 3);
    QCOMPARE(back->offset, 5ull << 30);
    QCOMPARE(back->length, quint32(65536));
    QVERIFY(!decodeRequest("{\"id\":1,\"what\":\"bogus\"}").has_value());
}

void TestClipMessages::chunkRoundTrip()
{
    const auto c = decodeChunk(encodeChunk({ 5, true, QByteArray() }));
    QVERIFY(c.has_value());
    QCOMPARE(c->id, quint32(5));
    QCOMPARE(c->last, true);
    QVERIFY(c->data.isEmpty());

    const auto d = decodeChunk(encodeChunk({ 6, false, QByteArray(3000, 'z') }));
    QCOMPARE(d->data.size(), 3000);
    QCOMPARE(d->last, false);
}

void TestClipMessages::chunkTooShortRejected()
{
    QVERIFY(!decodeChunk(QByteArray(4, 0)).has_value());
    QVERIFY(!decodeChunk(QByteArray()).has_value());
}

void TestClipMessages::errorRoundTrip()
{
    const auto e = decodeError(encodeError(9, "stale"));
    QVERIFY(e.has_value());
    QCOMPARE(e->first, quint32(9));
    QCOMPARE(e->second, QString("stale"));
}
