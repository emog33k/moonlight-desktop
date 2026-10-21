#include "clipreader_win32.h"

#include <QDir>
#include <QFileInfo>

#include <shellapi.h>
#include <shlobj.h>

namespace LocalRdp {
namespace Clip {

static CLIPFORMAT registered(const wchar_t* name)
{
    return (CLIPFORMAT)RegisterClipboardFormatW(name);
}

static bool hasFormat(IDataObject* data, CLIPFORMAT format)
{
    FORMATETC fmt = { format, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    return data->QueryGetData(&fmt) == S_OK;
}

static std::optional<QByteArray> getBytes(IDataObject* data, CLIPFORMAT format, bool* busy = nullptr)
{
    FORMATETC fmt = { format, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    STGMEDIUM medium = {};
    const HRESULT hr = data->GetData(&fmt, &medium);
    if (hr != S_OK) {
        if (busy && hr == CLIPBRD_E_CANT_OPEN) {
            *busy = true;
        }
        return std::nullopt;
    }
    std::optional<QByteArray> result;
    if (medium.tymed == TYMED_HGLOBAL && medium.hGlobal != nullptr) {
        const SIZE_T size = GlobalSize(medium.hGlobal);
        const void* p = GlobalLock(medium.hGlobal);
        if (p != nullptr) {
            result = QByteArray((const char*)p, (qsizetype)size);
            GlobalUnlock(medium.hGlobal);
        }
    }
    ReleaseStgMedium(&medium);
    return result;
}

static bool isPrivate(IDataObject* data)
{
    if (hasFormat(data, registered(L"ExcludeClipboardContentFromMonitorProcessing"))) {
        return true;
    }
    const auto history = getBytes(data, registered(L"CanIncludeInClipboardHistory"));
    if (history && history->size() >= (qsizetype)sizeof(DWORD)) {
        DWORD value;
        memcpy(&value, history->constData(), sizeof(value));
        return value == 0;
    }
    return false;
}

static QStringList droppedPaths(const QByteArray& hdrop)
{
    QStringList paths;
    if (hdrop.size() < (qsizetype)sizeof(DROPFILES)) {
        return paths;
    }
    DROPFILES df;
    memcpy(&df, hdrop.constData(), sizeof(df));
    if (!df.fWide || df.pFiles >= (DWORD)hdrop.size()) {
        return paths;
    }
    const char16_t* p = (const char16_t*)(hdrop.constData() + df.pFiles);
    const char16_t* end = (const char16_t*)(hdrop.constData() + hdrop.size());
    while (p < end && *p != 0) {
        const char16_t* start = p;
        while (p < end && *p != 0) {
            p++;
        }
        paths << QString::fromUtf16(start, p - start);
        p++;
    }
    return paths;
}

namespace {
struct Walk {
    QVector<FileEntry> entries;
    QStringList paths;
    quint64 total = 0;
    bool tooMuch = false;
    QString problem;

    void add(const QString& name, const QString& path, quint64 size, bool isDir)
    {
        if (tooMuch) {
            return;
        }
        total += size;
        if (entries.size() >= kMaxFiles) {
            problem = QStringLiteral("больше %1 файлов").arg(kMaxFiles);
        }
        else if (total > kMaxTotalFileBytes) {
            problem = QStringLiteral("больше %1 ГБ").arg(kMaxTotalFileBytes >> 30);
        }
        else if (!isSafeRelativeName(name)) {
            problem = QStringLiteral("слишком длинный путь или недопустимое имя: %1").arg(name.left(60));
        }
        if (!problem.isEmpty()) {
            tooMuch = true;
            return;
        }
        entries.append({ name, size, isDir });
        paths << (isDir ? QString() : path);
    }

    void walkDir(const QString& dirPath, const QString& relName)
    {
        const QFileInfoList children = QDir(dirPath).entryInfoList(
            QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System, QDir::Name | QDir::DirsFirst);
        for (const QFileInfo& child : children) {
            if (tooMuch) {
                return;
            }
            if (child.isSymLink() || child.isJunction()) {
                continue; // never follow links out of the copied tree
            }
            const QString name = relName + '\\' + child.fileName();
            if (child.isDir()) {
                add(name, QString(), 0, true);
                walkDir(child.absoluteFilePath(), name);
            }
            else {
                add(name, child.absoluteFilePath(), (quint64)child.size(), false);
            }
        }
    }
};
}

LocalSnapshot readDataObject(IDataObject* data, quint64 seq)
{
    LocalSnapshot s;
    s.offer.seq = seq;
    if (data == nullptr) {
        return s;
    }
    if (isPrivate(data)) {
        s.excluded = true;
        return s;
    }

    if (auto text = getBytes(data, CF_UNICODETEXT, &s.busy)) {
        // Up to the first terminator (HGLOBALs are often larger than the text)
        const char16_t* chars = (const char16_t*)text->constData();
        qsizetype n = 0;
        const qsizetype max = text->size() / 2;
        while (n < max && chars[n] != 0) {
            n++;
        }
        if ((quint64)n * 2 <= kMaxTextBytes) {
            s.text = text->left(n * 2);
            s.offer.textBytes = (quint64)s.text.size();
        }
    }

    if (auto dib = getBytes(data, CF_DIB, &s.busy)) {
        if (!dib->isEmpty() && (quint64)dib->size() <= kMaxImageBytes) {
            s.dib = *dib;
            s.offer.imageBytes = (quint64)dib->size();
        }
    }

    if (auto png = getBytes(data, registered(L"PNG"), &s.busy)) {
        if (!png->isEmpty() && (quint64)png->size() <= kMaxImageBytes) {
            s.png = *png;
            s.offer.pngBytes = (quint64)png->size();
        }
    }

    if (auto hdrop = getBytes(data, CF_HDROP, &s.busy)) {
        const LocalSnapshot files = snapshotFromPaths(droppedPaths(*hdrop), seq);
        s.offer.files = files.offer.files;
        s.filePaths = files.filePaths;
        s.filesProblem = files.filesProblem;
    }
    return s;
}

LocalSnapshot snapshotFromPaths(const QStringList& paths, quint64 seq)
{
    LocalSnapshot s;
    s.offer.seq = seq;
    Walk walk;
    int missing = 0;
    for (const QString& path : paths) {
        const QFileInfo info(path);
        if (!info.exists()) {
            missing++;
            continue;
        }
        if (info.isDir()) {
            walk.add(info.fileName(), QString(), 0, true);
            walk.walkDir(info.absoluteFilePath(), info.fileName());
        }
        else {
            walk.add(info.fileName(), info.absoluteFilePath(), (quint64)info.size(), false);
        }
    }
    if (!walk.tooMuch) {
        s.offer.files = walk.entries;
        s.filePaths = walk.paths;
    }
    else {
        s.filesProblem = walk.problem;
    }
    if (s.offer.files.isEmpty() && s.filesProblem.isEmpty() && !paths.isEmpty()) {
        s.filesProblem = missing > 0 ? QStringLiteral("файлы не найдены") : QStringLiteral("нечего отправлять");
    }
    return s;
}

}
}
