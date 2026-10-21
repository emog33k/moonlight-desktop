#include "clipmessages.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtEndian>

namespace LocalRdp {
namespace Clip {

static std::optional<QJsonObject> parseObject(const QByteArray& payload)
{
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(payload, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        return std::nullopt;
    }
    return doc.object();
}

// JSON numbers are doubles: exact up to 2^53, far beyond our limits.
static std::optional<quint64> readCount(const QJsonValue& v)
{
    if (!v.isDouble()) {
        return std::nullopt;
    }
    const double d = v.toDouble();
    if (d < 0 || d > 9007199254740992.0 || d != (double)(quint64)d) {
        return std::nullopt;
    }
    return (quint64)d;
}

bool isSafeRelativeName(const QString& name)
{
    if (name.isEmpty() || name.size() > kMaxNameLength) {
        return false;
    }
    for (const QChar c : name) {
        if (c.unicode() < 0x20 || QStringLiteral("<>:\"|?*").contains(c)) {
            return false;
        }
    }
    if (name.startsWith('\\') || name.startsWith('/')) {
        return false;
    }
    const QStringList parts = QString(name).replace('/', '\\').split('\\');
    for (const QString& part : parts) {
        if (part.isEmpty() || part == QLatin1String(".") || part == QLatin1String("..")) {
            return false;
        }
    }
    return true;
}

QByteArray encodeOffer(const Offer& offer)
{
    QJsonObject o{ { "seq", (double)offer.seq } };
    if (offer.textBytes) {
        o["text"] = (double)*offer.textBytes;
    }
    if (offer.imageBytes) {
        o["image"] = (double)*offer.imageBytes;
    }
    if (offer.pngBytes) {
        o["png"] = (double)*offer.pngBytes;
    }
    if (!offer.files.isEmpty()) {
        QJsonArray files;
        for (const FileEntry& f : offer.files) {
            files.append(QJsonObject{ { "n", f.name }, { "s", (double)f.size }, { "d", f.isDir } });
        }
        o["files"] = files;
    }
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

std::optional<Offer> decodeOffer(const QByteArray& payload)
{
    const auto o = parseObject(payload);
    if (!o) {
        return std::nullopt;
    }
    Offer offer;
    const auto seq = readCount(o->value("seq"));
    if (!seq) {
        return std::nullopt;
    }
    offer.seq = *seq;

    struct Field { const char* key; std::optional<quint64>* out; quint64 max; };
    const Field fields[] = {
        { "text", &offer.textBytes, kMaxTextBytes },
        { "image", &offer.imageBytes, kMaxImageBytes },
        { "png", &offer.pngBytes, kMaxImageBytes },
    };
    for (const Field& f : fields) {
        if (o->contains(f.key)) {
            const auto v = readCount(o->value(f.key));
            if (!v || *v > f.max) {
                return std::nullopt;
            }
            *f.out = v;
        }
    }

    if (o->contains("files")) {
        const QJsonValue filesValue = o->value("files");
        if (!filesValue.isArray()) {
            return std::nullopt;
        }
        const QJsonArray files = filesValue.toArray();
        if (files.size() > kMaxFiles) {
            return std::nullopt;
        }
        quint64 total = 0;
        for (const QJsonValue& v : files) {
            const QJsonObject f = v.toObject();
            FileEntry entry;
            entry.name = f.value("n").toString();
            const auto size = readCount(f.value("s"));
            entry.isDir = f.value("d").toBool();
            if (!size || !isSafeRelativeName(entry.name) || (entry.isDir && *size != 0)) {
                return std::nullopt;
            }
            entry.size = *size;
            total += entry.size;
            if (total > kMaxTotalFileBytes) {
                return std::nullopt;
            }
            offer.files.append(entry);
        }
    }
    return offer;
}

static const char* whatName(What what)
{
    switch (what) {
    case What::Text: return "text";
    case What::Image: return "image";
    case What::Png: return "png";
    case What::File: return "file";
    }
    return "text";
}

QByteArray encodeRequest(const Request& r)
{
    return QJsonDocument(QJsonObject{
        { "id", (double)r.id }, { "seq", (double)r.seq }, { "what", whatName(r.what) },
        { "index", r.index }, { "offset", (double)r.offset }, { "length", (double)r.length },
    }).toJson(QJsonDocument::Compact);
}

std::optional<Request> decodeRequest(const QByteArray& payload)
{
    const auto o = parseObject(payload);
    if (!o) {
        return std::nullopt;
    }
    Request r;
    const auto id = readCount(o->value("id"));
    const auto seq = readCount(o->value("seq"));
    const auto offset = readCount(o->value("offset"));
    const auto length = readCount(o->value("length"));
    const auto index = readCount(o->value("index"));
    if (!id || !seq || !offset || !length || !index || *id > 0xFFFFFFFFu || *length > (quint64)kClipChunk ||
            *index > (quint64)kMaxFiles) {
        return std::nullopt;
    }
    const QString what = o->value("what").toString();
    if (what == "text") r.what = What::Text;
    else if (what == "image") r.what = What::Image;
    else if (what == "png") r.what = What::Png;
    else if (what == "file") r.what = What::File;
    else return std::nullopt;

    r.id = (quint32)*id;
    r.seq = *seq;
    r.offset = *offset;
    r.length = (quint32)*length;
    r.index = (int)*index;
    return r;
}

QByteArray encodeChunk(const Chunk& c)
{
    QByteArray out(5, 0);
    qToBigEndian<quint32>(c.id, out.data());
    out[4] = c.last ? 1 : 0;
    out.append(c.data);
    return out;
}

std::optional<Chunk> decodeChunk(const QByteArray& payload)
{
    if (payload.size() < 5) {
        return std::nullopt;
    }
    Chunk c;
    c.id = qFromBigEndian<quint32>(payload.constData());
    c.last = payload[4] != 0;
    c.data = payload.mid(5);
    return c;
}

QByteArray encodeError(quint32 id, const QString& reason)
{
    return QJsonDocument(QJsonObject{ { "id", (double)id }, { "reason", reason } }).toJson(QJsonDocument::Compact);
}

std::optional<QPair<quint32, QString>> decodeError(const QByteArray& payload)
{
    const auto o = parseObject(payload);
    if (!o) {
        return std::nullopt;
    }
    const auto id = readCount(o->value("id"));
    if (!id || *id > 0xFFFFFFFFu) {
        return std::nullopt;
    }
    return qMakePair((quint32)*id, o->value("reason").toString());
}

}
}
