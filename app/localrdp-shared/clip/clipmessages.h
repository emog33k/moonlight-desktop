#pragma once

// Clipboard sync messages (inside the agent protocol's ClipOffer / ClipRequest /
// ClipData / ClipError frames). Everything received is validated here, so the rest
// of the code can trust a decoded message.

#include <QByteArray>
#include <QPair>
#include <QString>
#include <QVector>

#include <optional>

namespace LocalRdp {
namespace Clip {

constexpr int kClipChunk = 1 << 20;
constexpr int kMaxFiles = 10000;
constexpr quint64 kMaxTotalFileBytes = 64ull << 30;
constexpr quint64 kMaxTextBytes = 64ull << 20;
constexpr quint64 kMaxImageBytes = 256ull << 20;
constexpr int kMaxNameLength = 240;

struct FileEntry {
    QString name;      // relative to the copied items' parent, '\' separators
    quint64 size = 0;
    bool isDir = false;
};

// What the other side has on its clipboard (no data, only descriptions).
struct Offer {
    quint64 seq = 0;
    std::optional<quint64> textBytes;  // CF_UNICODETEXT, UTF-16LE bytes without terminator
    std::optional<quint64> imageBytes; // CF_DIB bytes
    std::optional<quint64> pngBytes;   // registered "PNG" format bytes
    QVector<FileEntry> files;
    // When this content was copied (ms since 1970, 0 = unknown). At connection
    // start both sides announce their clipboard ("initial"); only the newer one
    // is taken, so the clipboards don't swap.
    qint64 copiedAt = 0;
    bool initial = false;
    bool isEmpty() const { return !textBytes && !imageBytes && !pngBytes && files.isEmpty(); }
};

enum class What { Text, Image, Png, File };

struct Request {
    quint32 id = 0;
    quint64 seq = 0;
    What what = What::Text;
    int index = 0;        // file entry (What::File)
    quint64 offset = 0;   // within the file (What::File)
    quint32 length = 0;   // bytes wanted (What::File)
};

struct Chunk {
    quint32 id = 0;
    bool last = false;
    QByteArray data;
};

QByteArray encodeOffer(const Offer& offer);
std::optional<Offer> decodeOffer(const QByteArray& payload);

QByteArray encodeRequest(const Request& request);
std::optional<Request> decodeRequest(const QByteArray& payload);

QByteArray encodeChunk(const Chunk& chunk);                    // [u32 BE id][u8 last][data]
std::optional<Chunk> decodeChunk(const QByteArray& payload);

QByteArray encodeError(quint32 id, const QString& reason);
std::optional<QPair<quint32, QString>> decodeError(const QByteArray& payload);

// A name that can only ever mean "something inside the paste target folder",
// looks like what it is (no direction-changing characters) and can be created
// and deleted normally (no device names, no trailing dots or spaces).
bool isSafeRelativeName(const QString& name);

}
}
