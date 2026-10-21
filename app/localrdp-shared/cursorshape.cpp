#include "cursorshape.h"

#include <QtEndian>

namespace LocalRdp {
namespace Proto {

static const int kHeader = 8;

QByteArray encodeCursorImage(const CursorImage& image)
{
    QByteArray out(kHeader, 0);
    qToBigEndian<quint16>((quint16)image.width, out.data());
    qToBigEndian<quint16>((quint16)image.height, out.data() + 2);
    qToBigEndian<quint16>((quint16)image.hotX, out.data() + 4);
    qToBigEndian<quint16>((quint16)image.hotY, out.data() + 6);
    out += image.bgra;
    return out;
}

std::optional<CursorImage> decodeCursorImage(const QByteArray& payload)
{
    if (payload.size() < kHeader) {
        return std::nullopt;
    }
    CursorImage image;
    image.width = qFromBigEndian<quint16>(payload.constData());
    image.height = qFromBigEndian<quint16>(payload.constData() + 2);
    image.hotX = qFromBigEndian<quint16>(payload.constData() + 4);
    image.hotY = qFromBigEndian<quint16>(payload.constData() + 6);
    if (image.width < 1 || image.height < 1 || image.width > kMaxCursorSide || image.height > kMaxCursorSide ||
            image.hotX >= image.width || image.hotY >= image.height) {
        return std::nullopt;
    }
    if (payload.size() - kHeader != image.width * image.height * 4) {
        return std::nullopt;
    }
    image.bgra = payload.mid(kHeader);
    return image;
}

}
}
