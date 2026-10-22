#pragma once

#include <QtGlobal>

// Reconnecting by itself (like AnyDesk) when the network drops in the middle of
// a stream. The stream screen then shows "reconnecting" and tries again a
// number of times; the agent waits 15 s before locking the PC, so a quick
// reconnect does not lock it.

namespace LocalRdp {

constexpr qint64 kMinStreamedMs = 5000;   // it must have been running this long
constexpr int kMaxReconnectAttempts = 20;
constexpr int kReconnectDelayMs = 3000;   // between failed attempts

// errorCode: how the connection ended (moonlight-common-c's ML_ERROR_* or a
// network error code). Not when the PC ended the stream itself or something
// failed on its side: those show their message as before.
bool shouldAutoReconnect(int errorCode, qint64 streamedMs);

}
