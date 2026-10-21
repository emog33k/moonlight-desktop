#include "clipexchange.h"

#include <QFile>

#include <chrono>

namespace LocalRdp {
namespace Clip {

using Proto::MsgType;

void LocalClipSource::set(const Offer& offer, QByteArray text, QByteArray dib, QByteArray png, QStringList filePaths)
{
    std::lock_guard<std::mutex> lock(m_Mutex);
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
        if (!m_Has || request.seq != m_Offer.seq) {
            send(MsgType::ClipError, encodeError(request.id, "stale"));
            return;
        }
        switch (request.what) {
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
        m_Generation++;
        for (auto& p : m_Pending) {
            p.second.failed = true;
        }
    }
    m_Cv.notify_all();
}

void RemoteFetcher::disconnected()
{
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_HasOffer = false;
        m_Generation++;
        for (auto& p : m_Pending) {
            p.second.failed = true;
        }
    }
    m_Cv.notify_all();
}

std::optional<QByteArray> RemoteFetcher::fetchAll(What what, int timeoutMs)
{
    Request r;
    r.what = what;
    return run(r, timeoutMs);
}

std::optional<QByteArray> RemoteFetcher::fetchRange(int fileIndex, quint64 offset, quint32 length, int timeoutMs)
{
    Request r;
    r.what = What::File;
    r.index = fileIndex;
    r.offset = offset;
    r.length = qMin<quint32>(length, kClipChunk);
    return run(r, timeoutMs);
}

std::optional<QByteArray> RemoteFetcher::run(Request request, int timeoutMs)
{
    std::unique_lock<std::mutex> lock(m_Mutex);
    if (!m_HasOffer) {
        return std::nullopt;
    }
    request.id = m_NextId++;
    request.seq = m_Seq;
    const quint64 generation = m_Generation;
    Pending& pending = m_Pending[request.id];
    pending.generation = generation;

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
            result = std::move(p.data);
            break;
        }
        // The timeout is time without progress, so long transfers are fine
        const qsizetype seen = p.data.size();
        const bool woke = m_Cv.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&]() {
            const Pending& q = m_Pending[request.id];
            return q.done || q.failed || m_Generation != generation || q.data.size() != seen;
        });
        if (!woke) {
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
        it->second.data.append(chunk->data);
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
