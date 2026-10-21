#include "protocol.h"

#include <QJsonDocument>
#include <QtEndian>

namespace LocalRdp {
namespace Proto {

QByteArray encodeFrame(MsgType type, const QByteArray& payload)
{
    QByteArray frame(5, 0);
    qToBigEndian<quint32>((quint32)(payload.size() + 1), frame.data());
    frame[4] = (char)type;
    frame.append(payload);
    return frame;
}

void FrameDecoder::feed(const QByteArray& bytes)
{
    if (!m_Failed) {
        m_Buffer.append(bytes);
    }
}

std::optional<Frame> FrameDecoder::next()
{
    if (m_Failed || m_Buffer.size() < 4) {
        return std::nullopt;
    }
    const quint32 length = qFromBigEndian<quint32>(m_Buffer.constData());
    if (length == 0 || length > (quint32)kMaxFrame) {
        m_Failed = true;
        m_Buffer.clear();
        return std::nullopt;
    }
    if ((quint32)m_Buffer.size() < 4 + length) {
        return std::nullopt;
    }

    Frame frame;
    frame.type = (MsgType)(quint8)m_Buffer[4];
    frame.payload = m_Buffer.mid(5, (int)length - 1);
    m_Buffer.remove(0, 4 + (int)length);
    return frame;
}

QByteArray jsonPayload(const QJsonObject& object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

std::optional<QJsonObject> parseJsonPayload(const QByteArray& payload)
{
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(payload, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        return std::nullopt;
    }
    return doc.object();
}

}
}
