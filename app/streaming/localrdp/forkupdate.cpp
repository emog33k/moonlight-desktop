#include "forkupdate.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QUrl>
#include <QVector>

namespace LocalRdp {

// "1.2.3" -> {1, 2, 3}; empty if any part is not a number
static QVector<int> versionParts(const QString& version)
{
    QVector<int> parts;
    const QStringList list = version.split('.');
    for (const QString& p : list) {
        bool ok = false;
        const int n = p.toInt(&ok);
        if (!ok || n < 0) {
            return {};
        }
        parts << n;
    }
    return parts;
}

// Missing parts count as 0 ("1.0" == "1.0.0")
static int compareVersions(const QVector<int>& a, const QVector<int>& b)
{
    for (int i = 0; i < qMax(a.size(), b.size()); i++) {
        const int x = i < a.size() ? a[i] : 0;
        const int y = i < b.size() ? b[i] : 0;
        if (x != y) {
            return x < y ? -1 : 1;
        }
    }
    return 0;
}

std::optional<ForkUpdate> forkUpdateFrom(const QByteArray& latestReleaseJson, const QString& currentVersion)
{
    const QJsonDocument doc = QJsonDocument::fromJson(latestReleaseJson);
    if (!doc.isObject()) {
        return std::nullopt;
    }
    const QJsonObject o = doc.object();
    if (o.value("draft").toBool() || o.value("prerelease").toBool()) {
        return std::nullopt;
    }

    // Our tags are "desktop-v1.2.0" (upstream Moonlight's own "v1.x" tags are
    // different releases); the first release was plain "v1.0.0"
    QString version = o.value("tag_name").toString();
    if (version.startsWith(QLatin1String("desktop-"))) {
        version.remove(0, 8);
    }
    if (version.startsWith('v') || version.startsWith('V')) {
        version.remove(0, 1);
    }
    const QVector<int> latest = versionParts(version);
    const QVector<int> current = versionParts(currentVersion);
    if (latest.isEmpty() || current.isEmpty() || compareVersions(current, latest) >= 0) {
        return std::nullopt;
    }

    // The update button opens this page: only GitHub, only https
    const QUrl url(o.value("html_url").toString(), QUrl::StrictMode);
    if (!url.isValid() || url.scheme() != QLatin1String("https") || url.host() != QLatin1String("github.com")) {
        return std::nullopt;
    }
    return ForkUpdate{ version, url.toString() };
}

}
