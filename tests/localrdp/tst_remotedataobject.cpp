#include "tests.h"
#include "localrdp-shared/clip/clipexchange.h"
#include "localrdp-shared/clip/remotedataobject_win32.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <psapi.h>
#include <shlobj.h>

#include <atomic>
#include <memory>
#include <set>

using namespace LocalRdp;
using namespace LocalRdp::Clip;

namespace {

// Peer emulation: fetcher requests are served synchronously by a LocalClipSource.
struct Peer {
    LocalClipSource source;
    std::shared_ptr<RemoteFetcher> fetcher;
    std::atomic<int> requests { 0 };
    Peer()
    {
        fetcher = std::make_shared<RemoteFetcher>([this](Proto::MsgType type, QByteArray payload) {
            if (type != Proto::MsgType::ClipRequest) return;
            requests++;
            source.serve(*decodeRequest(payload), [this](Proto::MsgType t, QByteArray reply) {
                if (t == Proto::MsgType::ClipData) fetcher->onData(reply);
                else fetcher->onError(reply);
            });
        });
    }
    void offer(const Offer& o, QByteArray text, QByteArray dib, QStringList paths)
    {
        source.set(o, text, dib, {}, paths);
        fetcher->setOffer(o);
    }
};

CLIPFORMAT fmt(const wchar_t* name) { return (CLIPFORMAT)RegisterClipboardFormatW(name); }

QByteArray hglobalBytes(IDataObject* data, CLIPFORMAT format, HRESULT* hrOut = nullptr)
{
    FORMATETC f = { format, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    STGMEDIUM m = {};
    const HRESULT hr = data->GetData(&f, &m);
    if (hrOut) *hrOut = hr;
    if (hr != S_OK) return QByteArray();
    QByteArray out((const char*)GlobalLock(m.hGlobal), (qsizetype)GlobalSize(m.hGlobal));
    GlobalUnlock(m.hGlobal);
    ReleaseStgMedium(&m);
    return out;
}

QByteArray readStream(IDataObject* data, int index, HRESULT* hrOut = nullptr, SIZE_T* peakGrowth = nullptr)
{
    FORMATETC f = { fmt(CFSTR_FILECONTENTS), nullptr, DVASPECT_CONTENT, index, TYMED_ISTREAM };
    STGMEDIUM m = {};
    HRESULT hr = data->GetData(&f, &m);
    if (hrOut) *hrOut = hr;
    if (hr != S_OK) return QByteArray();
    QByteArray out;
    PROCESS_MEMORY_COUNTERS start = { sizeof(start) };
    GetProcessMemoryInfo(GetCurrentProcess(), &start, sizeof(start));
    SIZE_T peak = 0;
    std::vector<char> buf(256 * 1024);
    qint64 total = 0;
    for (;;) {
        ULONG got = 0;
        hr = m.pstm->Read(buf.data(), (ULONG)buf.size(), &got);
        if (FAILED(hr)) { if (hrOut) *hrOut = hr; break; }
        if (got == 0) break;
        total += got;
        if (peakGrowth) {
            PROCESS_MEMORY_COUNTERS now = { sizeof(now) };
            GetProcessMemoryInfo(GetCurrentProcess(), &now, sizeof(now));
            if (now.WorkingSetSize > start.WorkingSetSize) peak = qMax(peak, now.WorkingSetSize - start.WorkingSetSize);
        } else {
            out.append(buf.data(), got);
        }
    }
    if (peakGrowth) { *peakGrowth = peak; out = QByteArray::number(total); }
    ReleaseStgMedium(&m);
    return out;
}

}

void TestRemoteDataObject::enumFormats()
{
    Offer o;
    o.seq = 1;
    o.textBytes = 4;
    o.imageBytes = 8;
    o.files = { { "a.txt", 3, false } };
    Peer peer;
    IDataObject* data = createRemoteDataObject(o, peer.fetcher);

    IEnumFORMATETC* e = nullptr;
    QCOMPARE(data->EnumFormatEtc(DATADIR_GET, &e), S_OK);
    std::set<CLIPFORMAT> formats;
    FORMATETC f;
    while (e->Next(1, &f, nullptr) == S_OK) formats.insert(f.cfFormat);
    e->Release();

    for (CLIPFORMAT c : { (CLIPFORMAT)CF_UNICODETEXT, (CLIPFORMAT)CF_DIB, fmt(CFSTR_FILEDESCRIPTORW), fmt(CFSTR_FILECONTENTS),
                          fmt(CFSTR_PREFERREDDROPEFFECT), fmt(L"CanIncludeInClipboardHistory"), fmt(L"CanUploadToCloudClipboard") }) {
        QVERIFY2(formats.count(c), qPrintable(QString::number(c)));
    }
    QVERIFY(!formats.count(fmt(L"PNG")));
    data->Release();
}

void TestRemoteDataObject::textGetData()
{
    const QString text = "Строка с ПК";
    const QByteArray utf16((const char*)text.utf16(), text.size() * 2);
    Offer o;
    o.seq = 2;
    o.textBytes = utf16.size();
    Peer peer;
    peer.offer(o, utf16, {}, {});
    IDataObject* data = createRemoteDataObject(o, peer.fetcher);
    const QByteArray got = hglobalBytes(data, CF_UNICODETEXT);
    QCOMPARE(QString::fromUtf16((const char16_t*)got.constData()), text); // terminated
    data->Release();
}

void TestRemoteDataObject::dibGetData()
{
    const QByteArray dib(10000, 'b');
    Offer o;
    o.seq = 3;
    o.imageBytes = dib.size();
    Peer peer;
    peer.offer(o, {}, dib, {});
    IDataObject* data = createRemoteDataObject(o, peer.fetcher);
    QCOMPARE(hglobalBytes(data, CF_DIB).left(dib.size()), dib);
    data->Release();
}

void TestRemoteDataObject::fileDescriptorNamesSizes()
{
    Offer o;
    o.seq = 4;
    o.files = { { "Папка", 0, true }, { "Папка\\big.bin", 5000000000ull, false } };
    Peer peer;
    IDataObject* data = createRemoteDataObject(o, peer.fetcher);
    const QByteArray raw = hglobalBytes(data, fmt(CFSTR_FILEDESCRIPTORW));
    QVERIFY(raw.size() >= (qsizetype)sizeof(FILEGROUPDESCRIPTORW));
    const auto* group = (const FILEGROUPDESCRIPTORW*)raw.constData();
    QCOMPARE(group->cItems, UINT(2));
    QCOMPARE(QString::fromWCharArray(group->fgd[0].cFileName), QString("Папка"));
    QVERIFY(group->fgd[0].dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
    QCOMPARE(QString::fromWCharArray(group->fgd[1].cFileName), QString("Папка\\big.bin"));
    const quint64 size = ((quint64)group->fgd[1].nFileSizeHigh << 32) | group->fgd[1].nFileSizeLow;
    QCOMPARE(size, 5000000000ull);
    QVERIFY(group->fgd[1].dwFlags & FD_PROGRESSUI);
    data->Release();
}

void TestRemoteDataObject::fileContentsStreamMatchesSource()
{
    QTemporaryDir dir;
    QByteArray content(5 * 1024 * 1024 + 123, 0);
    for (int i = 0; i < content.size(); i++) content[i] = char(i % 251);
    QFile f(dir.filePath("x.bin"));
    f.open(QIODevice::WriteOnly);
    f.write(content);
    f.close();

    Offer o;
    o.seq = 5;
    o.files = { { "x.bin", (quint64)content.size(), false } };
    Peer peer;
    peer.offer(o, {}, {}, { f.fileName() });
    IDataObject* data = createRemoteDataObject(o, peer.fetcher);
    HRESULT hr = E_FAIL;
    const QByteArray got = readStream(data, 0, &hr);
    QCOMPARE(hr, S_OK);
    QCOMPARE(got.size(), content.size());
    QVERIFY(got == content);
    data->Release();
}

void TestRemoteDataObject::largeStreamBoundedMemory()
{
    QTemporaryDir dir;
    const qint64 size = 100ll * 1024 * 1024;
    QFile f(dir.filePath("large.bin"));
    f.open(QIODevice::WriteOnly);
    f.resize(size); // sparse/zero file, fast to create
    f.close();

    Offer o;
    o.seq = 6;
    o.files = { { "large.bin", (quint64)size, false } };
    Peer peer;
    peer.offer(o, {}, {}, { f.fileName() });
    IDataObject* data = createRemoteDataObject(o, peer.fetcher);
    SIZE_T growth = 0;
    const QByteArray total = readStream(data, 0, nullptr, &growth);
    QCOMPARE(total.toLongLong(), size);
    QVERIFY2(growth < 32u * 1024 * 1024, qPrintable(QString("grew %1 MB").arg(growth >> 20)));
    data->Release();
}

void TestRemoteDataObject::historyExclusionFormats()
{
    Offer o;
    o.seq = 7;
    o.textBytes = 2;
    Peer peer;
    IDataObject* data = createRemoteDataObject(o, peer.fetcher);
    for (const wchar_t* name : { L"CanIncludeInClipboardHistory", L"CanUploadToCloudClipboard" }) {
        const QByteArray v = hglobalBytes(data, fmt(name));
        QCOMPARE(v.size(), (qsizetype)sizeof(DWORD));
        QCOMPARE(*(const DWORD*)v.constData(), DWORD(0));
    }
    const QByteArray effect = hglobalBytes(data, fmt(CFSTR_PREFERREDDROPEFFECT));
    QCOMPARE(effect.size(), 0); // no files: no drop effect offered
    data->Release();
}

void TestRemoteDataObject::fetchFailureGivesError()
{
    Offer o;
    o.seq = 8;
    o.textBytes = 10;
    o.files = { { "gone.bin", 10, false } };
    Peer peer;
    peer.fetcher->setOffer(o); // the source side never got this offer: every request fails
    IDataObject* data = createRemoteDataObject(o, peer.fetcher);
    HRESULT hr = S_OK;
    hglobalBytes(data, CF_UNICODETEXT, &hr);
    QVERIFY(FAILED(hr));
    hr = S_OK;
    readStream(data, 0, &hr);
    QVERIFY(FAILED(hr));
    data->Release();
}

void TestRemoteDataObject::oldStreamFailsAfterNewOffer()
{
    QTemporaryDir dir;
    QFile a(dir.filePath("a.bin"));
    a.open(QIODevice::WriteOnly);
    a.write(QByteArray(3 * kClipChunk, 'A'));
    a.close();
    QFile b(dir.filePath("b.bin"));
    b.open(QIODevice::WriteOnly);
    b.write(QByteArray(3 * kClipChunk, 'B'));
    b.close();

    Offer first;
    first.seq = 1000;
    first.files = { { "x.bin", (quint64)3 * kClipChunk, false } };
    Peer peer;
    peer.offer(first, {}, {}, { a.fileName() });
    IDataObject* data = createRemoteDataObject(first, peer.fetcher);

    FORMATETC f = { (CLIPFORMAT)RegisterClipboardFormatW(CFSTR_FILECONTENTS), nullptr, DVASPECT_CONTENT, 0, TYMED_ISTREAM };
    STGMEDIUM m = {};
    QCOMPARE(data->GetData(&f, &m), S_OK);
    std::vector<char> buf(kClipChunk);
    ULONG got = 0;
    QCOMPARE(m.pstm->Read(buf.data(), (ULONG)buf.size(), &got), S_OK);
    QCOMPARE(buf[0], 'A');

    // The source copies something else; same name, same size
    Offer second = first;
    second.seq = 2000;
    peer.offer(second, {}, {}, { b.fileName() });

    const HRESULT hr = m.pstm->Read(buf.data(), (ULONG)buf.size(), &got);
    QVERIFY(FAILED(hr));
    ReleaseStgMedium(&m);
    data->Release();
}

void TestRemoteDataObject::textCachedAcrossGetData()
{
    const QByteArray utf16((const char*)u"abc", 6);
    Offer o;
    o.seq = 11;
    o.textBytes = utf16.size();
    Peer peer;
    peer.offer(o, utf16, {}, {});
    IDataObject* data = createRemoteDataObject(o, peer.fetcher);
    hglobalBytes(data, CF_UNICODETEXT);
    hglobalBytes(data, CF_UNICODETEXT);
    hglobalBytes(data, CF_UNICODETEXT);
    QCOMPARE(peer.requests.load(), 1);
    data->Release();
}
