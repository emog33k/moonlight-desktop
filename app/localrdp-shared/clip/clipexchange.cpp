#include "clipexchange.h"

#include <QFile>

#include <chrono>

namespace LocalRdp {
namespace Clip {

using Proto::MsgType;

void LocalClipSource::set(const Offer& offer, QByteArray text, QByteArray dib, QByteArray png, QStringList filePaths)
{
    std::lock_guard<std::mutex> lock(m_Mutex);
    if (m_Has && !m_Offer.files.isEmpty() && m_Offer.seq != offer.seq) {
        m_OlderFiles.prepend({ m_Offer, m_FilePaths });
        while (m_OlderFiles.size() > kKeptFileOffers) {
            m_OlderFiles.removeLast();
        }
    }
    m_Has = true;
    m_Offer = offer;
    m_Text = std::move(text);
    m_Dib = std::move(dib);
    m_Png = std::move(png);
    m_FilePaths = std::move(filePaths);
}

void LocalClipSource::clear()
{
    std::lock_guard<std::mutex> lock(m_Mutex);
    m_Has = false;
    m_Offer = Offer();
    m_Text.clear();
    m_Dib.clear();
    m_Png.clear();
    m_FilePaths.clear();
    m_OlderFiles.clear();
}

bool LocalClipSource::hasOffer() const
{
    std::lock_guard<std::mutex> lock(m_Mutex);
    return m_Has;
}

quint64 LocalClipSource::seq() const
{
    std::lock_guard<std::mutex> lock(m_Mutex);
    return m_Offer.seq;
}

static void sendInChunks(quint32 id, const QByteArray& data, const SendFn& send)
{
    if (data.isEmpty()) {
        send(MsgType::ClipData, encodeChunk({ id, true, QByteArray() }));
        return;
    }
    for (int pos = 0; pos < data.size(); pos += kClipChunk) {
        const bool last = pos + kClipChunk >= data.size();
        send(MsgType::ClipData, encodeChunk({ id, last, data.mid(pos, kClipChunk) }));
    }
}

void LocalClipSource::serve(const Request& request, const SendFn& send)
{
    QByteArray whole;
    QString path;
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        if (m_Has && request.what == What::File && request.seq != m_Offer.seq) {
            // A paste of an earlier copy that is still running
            for (const FileOffer& older : m_OlderFiles) {
                if (older.offer.seq != request.seq) {
                    continue;
                }
                if (request.index < 0 || request.index >= older.offer.files.size() ||
                        request.index >= older.paths.size() || older.offer.files[request.index].isDir ||
                        older.paths[request.index].isEmpty()) {
                    send(MsgType::ClipError, encodeError(request.id, "not a file"));
                    return;
                }
                path = older.paths[request.index];
                break;
            }
            if (path.isEmpty()) {
                send(MsgType::ClipError, encodeError(request.id, "stale"));
                return;
            }
        }
        else if (!m_Has || request.seq != m_Offer.seq) {
            send(MsgType::ClipError, encodeError(request.id, "stale"));
            return;
        }
        else switch (request.what) {
        case What::Text:
            if (!m_Offer.textBytes) { send(MsgType::ClipError, encodeError(request.id, "absent")); return; }
            whole = m_Text;
            break;
        case What::Image:
            if (!m_Offer.imageBytes) { send(MsgType::ClipError, encodeError(request.id, "absent")); return; }
            whole = m_Dib;
            break;
        case What::Png:
            if (!m_Offer.pngBytes) { send(MsgType::ClipError, encodeError(request.id, "absent")); return; }
            whole = m_Png;
            break;
        case What::File:
            if (request.index < 0 || request.index >= m_Offer.files.size() || request.index >= m_FilePaths.size() ||
                    m_Offer.files[request.index].isDir || m_FilePaths[request.index].isEmpty()) {
                send(MsgType::ClipError, encodeError(request.id, "not a file"));
                return;
            }
            path = m_FilePaths[request.index];
            break;
        }
    }

    if (request.what != What::File) {
        sendInChunks(request.id, whole, send);
        return;
    }

    // Read outside the lock: disk may be slow
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || !file.seek((qint64)request.offset)) {
        send(MsgType::ClipError, encodeError(request.id, "read failed"));
        return;
    }
    const qint64 want = qMin<qint64>(request.length, kClipChunk);
    const QByteArray data = file.read(want);
    if (data.isNull() && want > 0 && !file.atEnd()) {
        send(MsgType::ClipError, encodeError(request.id, "read failed"));
        return;
    }
    send(MsgType::ClipData, encodeChunk({ request.id, true, data }));
}

RemoteFetcher::RemoteFetcher(SendFn send)
    : m_Send(std::move(send))
{
}

void RemoteFetcher::setOffer(const Offer& offer)
{
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_HasOffer = true;
        m_Seq = offer.seq;
        m_Offer = offer;
        if (!offer.files.isEmpty() && !m_FileSeqs.contains(offer.seq)) {
            m_FileSeqs.prepend(offer.seq);
            while (m_FileSeqs.size() > LocalClipSource::kKeptFileOffers + 1) {
                m_FileSeqs.removeLast();
            }
        }
        // Text and images of the old offer are gone; file reads of recent offers go on
        for (auto& p : m_Pending) {
            if (!p.second.isFile || !m_FileSeqs.contains(p.second.seq)) {
                p.second.failed = true;
            }
        }
    }
    m_Cv.notify_all();
}

void RemoteFetcher::disconnected()
{
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_HasOffer = false;
        m_FileSeqs.clear();
        m_Generation++;
        for (auto& p : m_Pending) {
            p.second.failed = true;
        }
    }
    m_Cv.notify_all();
}

std::optional<QByteArray> RemoteFetcher::fetchAll(quint64 seq, What what, int timeoutMs)
{
    std::optional<quint64> size;
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        if (!m_HasOffer || seq != m_Seq) {
            return std::nullopt;
        }
        size = what == What::Text ? m_Offer.textBytes : what == What::Image ? m_Offer.imageBytes
             : what == What::Png ? m_Offer.pngBytes : std::nullopt;
    }
    if (!size) {
        return std::nullopt;
    }
    Request r;
    r.seq = seq;
    r.what = what;
    return run(r, (qsizetype)*size, (qsizetype)*size, timeoutMs);
}

std::optional<QByteArray> RemoteFetcher::fetchRange(quint64 seq, int fileIndex, quint64 offset, quint32 length,
                                                    int timeoutMs)
{
    Request r;
    r.seq = seq;
    r.what = What::File;
    r.index = fileIndex;
    r.offset = offset;
    r.length = qMin<quint32>(length, kClipChunk);
    return run(r, (qsizetype)r.length, -1, timeoutMs);
}

std::optional<QByteArray> RemoteFetcher::run(Request request, qsizetype limit, qsizetype exact, int timeoutMs)
{
    std::unique_lock<std::mutex> lock(m_Mutex);
    // Only for the offer the caller belongs to: an old paste never gets new data
    const bool isFile = request.what == What::File;
    if (!m_HasOffer || (request.seq != m_Seq && !(isFile && m_FileSeqs.contains(request.seq)))) {
        return std::nullopt;
    }
    request.id = m_NextId++;
    const quint64 generation = m_Generation;
    Pending& pending = m_Pending[request.id];
    pending.generation = generation;
    pending.limit = limit;
    pending.exact = exact;
    pending.isFile = isFile;
    pending.seq = request.seq;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kMaxRequestMs);

    lock.unlock();
    m_Send(MsgType::ClipRequest, encodeRequest(request));
    lock.lock();

    std::optional<QByteArray> result;
    for (;;) {
        Pending& p = m_Pending[request.id];
        if (p.failed || m_Generation != generation) {
            break;
        }
        if (p.done) {
            if (p.exact < 0 || p.data.size() == p.exact) {
                result = std::move(p.data);
            }
            break;
        }
        // The timeout is time without progress, so long transfers are fine;
        // a peer trickling bytes forever still hits the overall deadline.
        const quint64 seen = p.frames;
        const bool woke = m_Cv.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&]() {
            const Pending& q = m_Pending[request.id];
            return q.done || q.failed || m_Generation != generation || q.frames != seen;
        });
        if (!woke || std::chrono::steady_clock::now() > deadline) {
            break;
        }
    }
    m_Pending.erase(request.id);
    return result;
}

void RemoteFetcher::onData(const QByteArray& chunkPayload)
{
    const auto chunk = decodeChunk(chunkPayload);
    if (!chunk) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        auto it = m_Pending.find(chunk->id);
        if (it == m_Pending.end() || it->second.failed) {
            return; // cancelled or unknown: ignore
        }
        it->second.frames++;
        if (it->second.data.size() + chunk->data.size() > it->second.limit) {
            it->second.failed = true; // more than offered: refuse
        }
        else {
            it->second.data.append(chunk->data);
        }
        if (chunk->last) {
            it->second.done = true;
        }
    }
    m_Cv.notify_all();
}

void RemoteFetcher::onError(const QByteArray& errorPayload)
{
    const auto error = decodeError(errorPayload);
    if (!error) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        auto it = m_Pending.find(error->first);
        if (it != m_Pending.end()) {
            it->second.failed = true;
        }
    }
    m_Cv.notify_all();
}

}
}
