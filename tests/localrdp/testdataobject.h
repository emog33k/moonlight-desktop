#pragma once

// Minimal IDataObject for tests: holds HGLOBAL formats in memory, so clipboard
// reading code can be tested without touching the real clipboard.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <map>
#include <vector>

class TestDataObject : public IDataObject
{
public:
    void set(CLIPFORMAT format, const QByteArray& bytes) { m_Formats[format] = bytes; }
    // The format is listed, but reading it fails with hr (e.g. the clipboard is busy)
    void failGetData(CLIPFORMAT format, HRESULT hr) { m_Fail[format] = hr; }

    void setText(const QString& text)
    {
        QByteArray bytes((const char*)text.utf16(), (text.size() + 1) * 2); // with terminator
        set(CF_UNICODETEXT, bytes);
    }

    void setFiles(const QStringList& paths)
    {
        QByteArray list;
        for (const QString& p : paths) {
            const QString native = QString(p).replace('/', '\\');
            list.append((const char*)native.utf16(), (native.size() + 1) * 2);
        }
        list.append(2, 0); // final terminator
        DROPFILES df = {};
        df.pFiles = sizeof(DROPFILES);
        df.fWide = TRUE;
        QByteArray bytes((const char*)&df, sizeof(df));
        bytes.append(list);
        set(CF_HDROP, bytes);
    }

    void setDword(const wchar_t* formatName, DWORD value)
    {
        set((CLIPFORMAT)RegisterClipboardFormatW(formatName), QByteArray((const char*)&value, sizeof(value)));
    }

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override
    {
        if (riid == IID_IUnknown || riid == IID_IDataObject) {
            *ppv = static_cast<IDataObject*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++m_Refs; }
    STDMETHODIMP_(ULONG) Release() override { return --m_Refs; } // stack object in tests

    // IDataObject
    STDMETHODIMP GetData(FORMATETC* fmt, STGMEDIUM* medium) override
    {
        auto fail = m_Fail.find(fmt->cfFormat);
        if (fail != m_Fail.end()) {
            return fail->second;
        }
        auto it = m_Formats.find(fmt->cfFormat);
        if (it == m_Formats.end() || !(fmt->tymed & TYMED_HGLOBAL)) {
            return DV_E_FORMATETC;
        }
        HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, std::max<SIZE_T>(1, it->second.size()));
        void* p = GlobalLock(h);
        memcpy(p, it->second.constData(), it->second.size());
        GlobalUnlock(h);
        medium->tymed = TYMED_HGLOBAL;
        medium->hGlobal = h;
        medium->pUnkForRelease = nullptr;
        return S_OK;
    }
    STDMETHODIMP GetDataHere(FORMATETC*, STGMEDIUM*) override { return E_NOTIMPL; }
    STDMETHODIMP QueryGetData(FORMATETC* fmt) override
    {
        return m_Formats.count(fmt->cfFormat) ? S_OK : DV_E_FORMATETC;
    }
    STDMETHODIMP GetCanonicalFormatEtc(FORMATETC*, FORMATETC* out) override { out->ptd = nullptr; return E_NOTIMPL; }
    STDMETHODIMP SetData(FORMATETC*, STGMEDIUM*, BOOL) override { return E_NOTIMPL; }
    STDMETHODIMP EnumFormatEtc(DWORD dir, IEnumFORMATETC** out) override
    {
        if (dir != DATADIR_GET) {
            return E_NOTIMPL;
        }
        std::vector<FORMATETC> list;
        for (const auto& f : m_Formats) {
            list.push_back({ f.first, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL });
        }
        return SHCreateStdEnumFmtEtc((UINT)list.size(), list.data(), out);
    }
    STDMETHODIMP DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override { return OLE_E_ADVISENOTSUPPORTED; }
    STDMETHODIMP DUnadvise(DWORD) override { return OLE_E_ADVISENOTSUPPORTED; }
    STDMETHODIMP EnumDAdvise(IEnumSTATDATA**) override { return OLE_E_ADVISENOTSUPPORTED; }

private:
    std::map<CLIPFORMAT, QByteArray> m_Formats;
    std::map<CLIPFORMAT, HRESULT> m_Fail;
    ULONG m_Refs = 1;
};
