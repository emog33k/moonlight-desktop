#include "remotedataobject_win32.h"

#include <shlobj.h>
#include <shobjidl.h>

#include <atomic>
#include <vector>

namespace LocalRdp {
namespace Clip {

namespace {

CLIPFORMAT registered(const wchar_t* name)
{
    return (CLIPFORMAT)RegisterClipboardFormatW(name);
}

HGLOBAL globalFrom(const void* data, SIZE_T size)
{
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, size > 0 ? size : 1);
    if (h == nullptr) {
        return nullptr;
    }
    if (size > 0) {
        void* p = GlobalLock(h);
        memcpy(p, data, size);
        GlobalUnlock(h);
    }
    return h;
}

// One virtual file, read sequentially by Explorer.
class RemoteFileStream : public IStream
{
public:
    RemoteFileStream(std::shared_ptr<RemoteFetcher> fetcher, quint64 seq, int index, quint64 size, QString name)
        : m_Fetcher(std::move(fetcher)), m_Seq(seq), m_Index(index), m_Size(size), m_Name(std::move(name)) {}

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override
    {
        if (riid == IID_IUnknown || riid == IID_IStream || riid == IID_ISequentialStream) {
            *ppv = static_cast<IStream*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++m_Refs; }
    STDMETHODIMP_(ULONG) Release() override
    {
        const ULONG refs = --m_Refs;
        if (refs == 0) delete this;
        return refs;
    }

    STDMETHODIMP Read(void* pv, ULONG cb, ULONG* pcbRead) override
    {
        ULONG done = 0;
        while (done < cb && m_Pos < m_Size) {
            if (m_BufferPos >= m_Buffer.size()) {
                const quint32 want = (quint32)qMin<quint64>(kClipChunk, m_Size - m_Pos);
                auto data = m_Fetcher->fetchRange(m_Seq, m_Index, m_Pos, want);
                if (!data || data->isEmpty()) {
                    if (pcbRead) *pcbRead = done;
                    return STG_E_READFAULT;
                }
                m_Buffer = std::move(*data);
                m_BufferPos = 0;
            }
            const ULONG n = (ULONG)qMin<qsizetype>(cb - done, m_Buffer.size() - m_BufferPos);
            memcpy((char*)pv + done, m_Buffer.constData() + m_BufferPos, n);
            m_BufferPos += n;
            m_Pos += n;
            done += n;
        }
        if (pcbRead) *pcbRead = done;
        return (done < cb) ? S_FALSE : S_OK;
    }
    STDMETHODIMP Write(const void*, ULONG, ULONG*) override { return STG_E_ACCESSDENIED; }

    STDMETHODIMP Seek(LARGE_INTEGER move, DWORD origin, ULARGE_INTEGER* newPos) override
    {
        qint64 base = 0;
        switch (origin) {
        case STREAM_SEEK_SET: base = 0; break;
        case STREAM_SEEK_CUR: base = (qint64)m_Pos; break;
        case STREAM_SEEK_END: base = (qint64)m_Size; break;
        default: return STG_E_INVALIDFUNCTION;
        }
        const qint64 target = base + move.QuadPart;
        if (target < 0) {
            return STG_E_INVALIDFUNCTION;
        }
        if ((quint64)target != m_Pos) {
            m_Pos = (quint64)target;
            m_Buffer.clear();
            m_BufferPos = 0;
        }
        if (newPos) newPos->QuadPart = m_Pos;
        return S_OK;
    }
    STDMETHODIMP SetSize(ULARGE_INTEGER) override { return E_NOTIMPL; }
    STDMETHODIMP CopyTo(IStream*, ULARGE_INTEGER, ULARGE_INTEGER*, ULARGE_INTEGER*) override { return E_NOTIMPL; }
    STDMETHODIMP Commit(DWORD) override { return S_OK; }
    STDMETHODIMP Revert() override { return E_NOTIMPL; }
    STDMETHODIMP LockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return STG_E_INVALIDFUNCTION; }
    STDMETHODIMP UnlockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return STG_E_INVALIDFUNCTION; }
    STDMETHODIMP Stat(STATSTG* stat, DWORD flags) override
    {
        ZeroMemory(stat, sizeof(*stat));
        stat->type = STGTY_STREAM;
        stat->cbSize.QuadPart = m_Size;
        stat->grfMode = STGM_READ;
        if (!(flags & STATFLAG_NONAME)) {
            const std::wstring name = m_Name.toStdWString();
            const size_t bytes = (name.size() + 1) * sizeof(wchar_t);
            stat->pwcsName = (LPOLESTR)CoTaskMemAlloc(bytes);
            if (stat->pwcsName) memcpy(stat->pwcsName, name.c_str(), bytes);
        }
        return S_OK;
    }
    STDMETHODIMP Clone(IStream**) override { return E_NOTIMPL; }

private:
    std::shared_ptr<RemoteFetcher> m_Fetcher;
    quint64 m_Seq;
    int m_Index;
    quint64 m_Size;
    QString m_Name;
    quint64 m_Pos = 0;
    QByteArray m_Buffer;
    qsizetype m_BufferPos = 0;
    std::atomic<ULONG> m_Refs { 1 };
};

class RemoteDataObject : public IDataObject, public IDataObjectAsyncCapability
{
public:
    RemoteDataObject(const Offer& offer, std::shared_ptr<RemoteFetcher> fetcher)
        : m_Offer(offer), m_Fetcher(std::move(fetcher))
    {
        m_CfFileDescriptor = registered(CFSTR_FILEDESCRIPTORW);
        m_CfFileContents = registered(CFSTR_FILECONTENTS);
        m_CfDropEffect = registered(CFSTR_PREFERREDDROPEFFECT);
        m_CfPng = registered(L"PNG");
        m_CfHistory = registered(L"CanIncludeInClipboardHistory");
        m_CfCloud = registered(L"CanUploadToCloudClipboard");

        auto add = [this](CLIPFORMAT cf, DWORD tymed, LONG index = -1) {
            m_Formats.push_back({ cf, nullptr, DVASPECT_CONTENT, index, tymed });
        };
        if (m_Offer.textBytes) add(CF_UNICODETEXT, TYMED_HGLOBAL);
        if (m_Offer.imageBytes) add(CF_DIB, TYMED_HGLOBAL);
        if (m_Offer.pngBytes) add(m_CfPng, TYMED_HGLOBAL);
        if (!m_Offer.files.isEmpty()) {
            add(m_CfFileDescriptor, TYMED_HGLOBAL);
            add(m_CfFileContents, TYMED_ISTREAM);
            add(m_CfDropEffect, TYMED_HGLOBAL);
        }
        add(m_CfHistory, TYMED_HGLOBAL);
        add(m_CfCloud, TYMED_HGLOBAL);
    }

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override
    {
        if (riid == IID_IUnknown || riid == IID_IDataObject) {
            *ppv = static_cast<IDataObject*>(this);
        } else if (riid == __uuidof(IDataObjectAsyncCapability)) {
            *ppv = static_cast<IDataObjectAsyncCapability*>(this);
        } else {
            *ppv = nullptr;
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++m_Refs; }
    STDMETHODIMP_(ULONG) Release() override
    {
        const ULONG refs = --m_Refs;
        if (refs == 0) delete this;
        return refs;
    }

    // IDataObject
    STDMETHODIMP GetData(FORMATETC* f, STGMEDIUM* m) override
    {
        if (f == nullptr || m == nullptr) return E_INVALIDARG;
        ZeroMemory(m, sizeof(*m));
        const CLIPFORMAT cf = f->cfFormat;

        if (cf == m_CfFileContents) {
            if (!(f->tymed & TYMED_ISTREAM) || f->lindex < 0 || f->lindex >= m_Offer.files.size() ||
                    m_Offer.files[f->lindex].isDir) {
                return DV_E_FORMATETC;
            }
            const FileEntry& e = m_Offer.files[f->lindex];
            m->tymed = TYMED_ISTREAM;
            m->pstm = new RemoteFileStream(m_Fetcher, m_Offer.seq, (int)f->lindex, e.size, e.name);
            return S_OK;
        }
        if (!(f->tymed & TYMED_HGLOBAL)) {
            return DV_E_TYMED;
        }

        HGLOBAL h = nullptr;
        if (cf == CF_UNICODETEXT && m_Offer.textBytes) {
            const QByteArray* data = cached(m_Text, What::Text);
            if (!data) return E_FAIL;
            QByteArray terminated = *data;
            terminated.append(2, 0);
            h = globalFrom(terminated.constData(), terminated.size());
        } else if (cf == CF_DIB && m_Offer.imageBytes) {
            const QByteArray* data = cached(m_Dib, What::Image);
            if (!data) return E_FAIL;
            h = globalFrom(data->constData(), data->size());
        } else if (cf == m_CfPng && m_Offer.pngBytes) {
            const QByteArray* data = cached(m_Png, What::Png);
            if (!data) return E_FAIL;
            h = globalFrom(data->constData(), data->size());
        } else if (cf == m_CfFileDescriptor && !m_Offer.files.isEmpty()) {
            h = fileGroupDescriptor();
        } else if (cf == m_CfDropEffect && !m_Offer.files.isEmpty()) {
            const DWORD effect = DROPEFFECT_COPY;
            h = globalFrom(&effect, sizeof(effect));
        } else if (cf == m_CfHistory || cf == m_CfCloud) {
            // Synced content never goes to Windows clipboard history or the cloud
            const DWORD no = 0;
            h = globalFrom(&no, sizeof(no));
        } else {
            return DV_E_FORMATETC;
        }

        if (h == nullptr) return E_OUTOFMEMORY;
        m->tymed = TYMED_HGLOBAL;
        m->hGlobal = h;
        return S_OK;
    }
    STDMETHODIMP GetDataHere(FORMATETC*, STGMEDIUM*) override { return E_NOTIMPL; }
    STDMETHODIMP QueryGetData(FORMATETC* f) override
    {
        if (f == nullptr) return E_INVALIDARG;
        for (const FORMATETC& mine : m_Formats) {
            if (mine.cfFormat == f->cfFormat) {
                return (mine.tymed & f->tymed) ? S_OK : DV_E_TYMED;
            }
        }
        return DV_E_FORMATETC;
    }
    STDMETHODIMP GetCanonicalFormatEtc(FORMATETC*, FORMATETC* out) override
    {
        if (out) out->ptd = nullptr;
        return DATA_S_SAMEFORMATETC;
    }
    STDMETHODIMP SetData(FORMATETC*, STGMEDIUM* m, BOOL release) override
    {
        // Explorer reports "performed drop effect" etc. here; nothing to keep.
        if (release && m) ReleaseStgMedium(m);
        return S_OK;
    }
    STDMETHODIMP EnumFormatEtc(DWORD dir, IEnumFORMATETC** out) override
    {
        if (dir != DATADIR_GET) return E_NOTIMPL;
        return SHCreateStdEnumFmtEtc((UINT)m_Formats.size(), m_Formats.data(), out);
    }
    STDMETHODIMP DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override { return OLE_E_ADVISENOTSUPPORTED; }
    STDMETHODIMP DUnadvise(DWORD) override { return OLE_E_ADVISENOTSUPPORTED; }
    STDMETHODIMP EnumDAdvise(IEnumSTATDATA**) override { return OLE_E_ADVISENOTSUPPORTED; }

    // IDataObjectAsyncCapability: lets Explorer copy on a background thread with progress
    STDMETHODIMP SetAsyncMode(BOOL async) override { m_Async = async; return S_OK; }
    STDMETHODIMP GetAsyncMode(BOOL* async) override { *async = m_Async; return S_OK; }
    STDMETHODIMP StartOperation(IBindCtx*) override { m_InOperation = TRUE; return S_OK; }
    STDMETHODIMP InOperation(BOOL* in) override { *in = m_InOperation; return S_OK; }
    STDMETHODIMP EndOperation(HRESULT, IBindCtx*, DWORD) override { m_InOperation = FALSE; return S_OK; }

private:
    // Fetched once per offer; later pastes and format probes reuse it
    const QByteArray* cached(std::optional<QByteArray>& slot, What what)
    {
        if (!slot) {
            slot = m_Fetcher->fetchAll(m_Offer.seq, what);
        }
        return slot ? &*slot : nullptr;
    }

    HGLOBAL fileGroupDescriptor()
    {
        const int n = m_Offer.files.size();
        const SIZE_T bytes = sizeof(FILEGROUPDESCRIPTORW) + (n - 1) * sizeof(FILEDESCRIPTORW);
        HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, bytes);
        if (h == nullptr) return nullptr;
        auto* group = (FILEGROUPDESCRIPTORW*)GlobalLock(h);
        group->cItems = (UINT)n;
        for (int i = 0; i < n; i++) {
            const FileEntry& e = m_Offer.files[i];
            FILEDESCRIPTORW& d = group->fgd[i];
            d.dwFlags = FD_ATTRIBUTES | FD_FILESIZE | FD_PROGRESSUI | FD_UNICODE;
            d.dwFileAttributes = e.isDir ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
            d.nFileSizeHigh = (DWORD)(e.size >> 32);
            d.nFileSizeLow = (DWORD)(e.size & 0xFFFFFFFFu);
            const std::wstring name = e.name.toStdWString();
            wcsncpy_s(d.cFileName, MAX_PATH, name.c_str(), _TRUNCATE);
        }
        GlobalUnlock(h);
        return h;
    }

    Offer m_Offer;
    std::shared_ptr<RemoteFetcher> m_Fetcher;
    std::vector<FORMATETC> m_Formats;
    std::optional<QByteArray> m_Text, m_Dib, m_Png;
    CLIPFORMAT m_CfFileDescriptor, m_CfFileContents, m_CfDropEffect, m_CfPng, m_CfHistory, m_CfCloud;
    std::atomic<ULONG> m_Refs { 1 };
    BOOL m_Async = TRUE;
    BOOL m_InOperation = FALSE;
};

}

IDataObject* createRemoteDataObject(const Offer& offer, std::shared_ptr<RemoteFetcher> fetcher)
{
    return new RemoteDataObject(offer, std::move(fetcher));
}

}
}
