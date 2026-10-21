#pragma once

// The PC's mouse cursor, sent by the agent so the laptop can show it as its own
// cursor (AnyDesk style) while Sunshine leaves it out of the video.
//
// CursorShape payload: [u16 width][u16 height][u16 hotX][u16 hotY] then
// width*height pixels of 32-bit B,G,R,A (straight alpha), rows top to bottom.
// CursorHidden / CursorDefault have no payload.

#include <QByteArray>

#include <optional>

namespace LocalRdp {
namespace Proto {

constexpr int kMaxCursorSide = 256;

struct CursorImage {
    int width = 0;
    int height = 0;
    int hotX = 0;
    int hotY = 0;
    QByteArray bgra; // width * height * 4 bytes

    bool operator==(const CursorImage& o) const
    {
        return width == o.width && height == o.height && hotX == o.hotX && hotY == o.hotY && bgra == o.bgra;
    }
};

enum class CursorKind {
    Default, // unknown shape (e.g. the PC is on the lock screen): a plain arrow
    Hidden,  // the PC shows no cursor
    Shape,
};

struct CursorState {
    CursorKind kind = CursorKind::Default;
    CursorImage image; // kind == Shape
    quint64 key = 0;   // agent side: identifies the shape for change detection
};

QByteArray encodeCursorImage(const CursorImage& image);
// Rejects sizes outside 1..kMaxCursorSide, a hotspot outside the image and a
// pixel buffer of the wrong length.
std::optional<CursorImage> decodeCursorImage(const QByteArray& payload);

}
}
