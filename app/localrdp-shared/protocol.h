#pragma once

// Wire format between Moonlight (laptop) and LocalRDP Agent (PC), carried inside
// mutually authenticated TLS. A frame is
//     [u32 big-endian length of (type + payload)] [u8 type] [payload]
// Control messages carry a JSON object; data messages carry raw bytes.

#include <QByteArray>
#include <QJsonObject>

#include <optional>

namespace LocalRdp {
namespace Proto {

enum class MsgType : quint8 {
    Hello = 1,
    HelloAck = 2,
    Ping = 3,
    Pong = 4,
    Goodbye = 5,

    LockNow = 10,
    SetLockOnDisconnect = 11,

    ClipOffer = 20,
    ClipRequest = 21,
    ClipData = 22,
    ClipError = 23,

    DisplayList = 30,   // agent -> laptop, see hostdisplays.h
};

constexpr quint32 kProtocolVersion = 1;
constexpr int kMaxFrame = 16 * 1024 * 1024; // type + payload
constexpr quint16 kAgentPort = 48100;

struct Frame {
    MsgType type;
    QByteArray payload;
};

QByteArray encodeFrame(MsgType type, const QByteArray& payload);

// Reassembles frames from a byte stream. Once a malformed frame (zero length or
// larger than kMaxFrame) is seen it stays failed; the connection must be closed.
class FrameDecoder
{
public:
    void feed(const QByteArray& bytes);
    std::optional<Frame> next();
    bool failed() const { return m_Failed; }

private:
    QByteArray m_Buffer;
    bool m_Failed = false;
};

QByteArray jsonPayload(const QJsonObject& object);
// Only a JSON object is accepted.
std::optional<QJsonObject> parseJsonPayload(const QByteArray& payload);

}
}
