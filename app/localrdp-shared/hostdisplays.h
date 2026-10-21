#pragma once

// The PC's monitors as Sunshine numbers them (Ctrl+Alt+Shift+F1 = the first),
// sent by the agent in a DisplayList message:
//     {"displays":[{"name":"\\\\.\\DISPLAY1","width":1920,"height":1080,"primary":true}, ...],
//      "initial": 0}
// "initial" is the monitor Sunshine starts a stream on (-1 if unknown).

#include <QByteArray>
#include <QString>
#include <QVector>

#include <optional>

namespace LocalRdp {
namespace Proto {

struct HostDisplay {
    QString name;    // GDI device name, e.g. \\.\DISPLAY1
    int width = 0;
    int height = 0;
    bool primary = false;

    bool operator==(const HostDisplay& o) const
    {
        return name == o.name && width == o.width && height == o.height && primary == o.primary;
    }
};

struct DisplayList {
    QVector<HostDisplay> displays;
    int initial = -1;

    bool operator==(const DisplayList& o) const { return displays == o.displays && initial == o.initial; }
};

constexpr int kMaxDisplays = 16;

QByteArray encodeDisplayList(const DisplayList& list);
// Rejects anything malformed (too many entries, impossible sizes, long names);
// an out-of-range "initial" becomes -1.
std::optional<DisplayList> decodeDisplayList(const QByteArray& payload);

// The monitor Sunshine starts on, from its sunshine.conf: no output_name means
// the first one; a \\.\DISPLAYn name is looked up; anything else (e.g. a
// display-device id) is unknown (-1).
int initialDisplayFromConfig(const QVector<HostDisplay>& displays, const QByteArray& sunshineConf);

}
}
