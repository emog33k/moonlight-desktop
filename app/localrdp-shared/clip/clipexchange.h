#pragma once

#include "clipmessages.h"
#include "localrdp-shared/protocol.h"

#include <QStringList>

#include <condition_variable>
#include <functional>
#include <map>
#include <mutex>

namespace LocalRdp {
namespace Clip {

using SendFn = std::function<void(Proto::MsgType, QByteArray)>;

// What we offered to the peer (snapshot), answering its requests. Thread-safe.
// Only the snapshot's own data and files are ever served, by index.
class LocalClipSource
{
public:
    void set(const Offer& offer, QByteArray text, QByteArray dib, QByteArray png, QStringList filePaths);
    void clear();
    bool hasOffer() const;
    quint64 seq() const;

    // Sends the reply to one request: ClipData chunk(s) or a ClipError.
    void serve(const Request& request, const SendFn& send);

private:
    mutable std::mutex m_Mutex;
    bool m_Has = false;
    Offer m_Offer;
    QByteArray m_Text, m_Dib, m_Png;
    QStringList m_FilePaths; // one per offer.files entry; empty for directories
};

// Fetches the peer's clipboard data on demand. fetch* block the calling thread
// (the clipboard thread), never the network thread, which delivers replies via
// onData/onError. A new offer or a disconnect cancels every wait at once.
class RemoteFetcher
{
public:
    explicit RemoteFetcher(SendFn send);

    void setOffer(const Offer& offer);
    void disconnected();

    // Whole text/image/png. timeoutMs is the longest wait without progress.
    // seq: the offer the caller belongs to; a newer offer makes the call fail.
    std::optional<QByteArray> fetchAll(quint64 seq, What what, int timeoutMs = 15000);
    // Part of file `fileIndex` (at most kClipChunk bytes; shorter at end of file).
    std::optional<QByteArray> fetchRange(quint64 seq, int fileIndex, quint64 offset, quint32 length,
                                         int timeoutMs = 15000);

    void onData(const QByteArray& chunkPayload);
    void onError(const QByteArray& errorPayload);

private:
    struct Pending {
        quint64 generation = 0;
        QByteArray data;
        bool done = false;
        bool failed = false;
        qsizetype limit = 0;   // never more than the offer said
        qsizetype exact = -1;  // whole-item fetches must match the offered size
    };
    std::optional<QByteArray> run(Request request, qsizetype limit, qsizetype exact, int timeoutMs);

    SendFn m_Send;
    std::mutex m_Mutex;
    std::condition_variable m_Cv;
    bool m_HasOffer = false;
    quint64 m_Seq = 0;
    Offer m_Offer;
    quint64 m_Generation = 0;
    quint32 m_NextId = 1;
    std::map<quint32, Pending> m_Pending;
};

}
}
