#include "hostdisplays.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>

namespace LocalRdp {
namespace Proto {

static const int kMaxNameLength = 64;
static const int kMaxSide = 32768;

QByteArray encodeDisplayList(const DisplayList& list)
{
    QJsonArray displays;
    for (const HostDisplay& d : list.displays) {
        displays << QJsonObject{
            { "name", d.name },
            { "width", d.width },
            { "height", d.height },
            { "primary", d.primary },
        };
    }
    return QJsonDocument(QJsonObject{ { "displays", displays }, { "initial", list.initial } })
            .toJson(QJsonDocument::Compact);
}

std::optional<DisplayList> decodeDisplayList(const QByteArray& payload)
{
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(payload, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        return std::nullopt;
    }
    const QJsonObject o = doc.object();
    if (!o.value("displays").isArray()) {
        return std::nullopt;
    }
    const QJsonArray displays = o.value("displays").toArray();
    if (displays.size() > kMaxDisplays) {
        return std::nullopt;
    }

    DisplayList list;
    for (const QJsonValue& v : displays) {
        if (!v.isObject()) {
            return std::nullopt;
        }
        const QJsonObject e = v.toObject();
        HostDisplay d;
        d.name = e.value("name").toString();
        d.width = e.value("width").toInt();
        d.height = e.value("height").toInt();
        d.primary = e.value("primary").toBool();
        if (d.name.size() > kMaxNameLength || d.width <= 0 || d.height <= 0 ||
                d.width > kMaxSide || d.height > kMaxSide) {
            return std::nullopt;
        }
        list.displays << d;
    }
    const int initial = o.value("initial").toInt(-1);
    list.initial = (initial >= 0 && initial < list.displays.size()) ? initial : -1;
    return list;
}

int initialDisplayFromConfig(const QVector<HostDisplay>& displays, const QByteArray& sunshineConf)
{
    if (displays.isEmpty()) {
        return -1;
    }

    QString outputName;
    const QList<QByteArray> lines = sunshineConf.split('\n');
    for (const QByteArray& raw : lines) {
        const QString line = QString::fromUtf8(raw).trimmed();
        if (line.startsWith('#')) {
            continue;
        }
        const int eq = line.indexOf('=');
        if (eq < 0 || line.left(eq).trimmed() != QLatin1String("output_name")) {
            continue;
        }
        outputName = line.mid(eq + 1).trimmed();
    }

    // Sunshine starts on the first monitor unless one is configured
    if (outputName.isEmpty()) {
        return 0;
    }
    for (int i = 0; i < displays.size(); i++) {
        if (displays[i].name.compare(outputName, Qt::CaseInsensitive) == 0) {
            return i;
        }
    }
    return -1;
}

}
}
