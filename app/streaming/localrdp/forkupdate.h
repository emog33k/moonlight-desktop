#pragma once

#include <QByteArray>
#include <QString>

#include <optional>

// Update check for this fork: its GitHub releases, not the upstream Moonlight
// manifest (that would offer a Moonlight without this fork's features).

namespace LocalRdp {

// GitHub API: the latest published release of the fork
constexpr const char* kForkLatestReleaseUrl = "https://api.github.com/repos/emog33k/moonlight-desktop/releases/latest";

struct ForkUpdate {
    QString version; // e.g. "1.1.0"
    QString url;     // the release page
};

// From the GitHub "latest release" JSON: the release when it is newer than
// currentVersion ("x.y.z"). None for drafts, pre-releases, malformed answers,
// a page outside github.com, or a version not newer.
std::optional<ForkUpdate> forkUpdateFrom(const QByteArray& latestReleaseJson, const QString& currentVersion);

}
