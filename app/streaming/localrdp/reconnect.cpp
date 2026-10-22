#include "reconnect.h"

namespace LocalRdp {

// Same values as moonlight-common-c's Limelight.h (checked in session.cpp)
static const int kGraceful = 0;               // ML_ERROR_GRACEFUL_TERMINATION
static const int kUnexpectedEarlyEnd = -102;  // ML_ERROR_UNEXPECTED_EARLY_TERMINATION
static const int kProtectedContent = -103;    // ML_ERROR_PROTECTED_CONTENT
static const int kFrameConversion = -104;     // ML_ERROR_FRAME_CONVERSION

bool shouldAutoReconnect(int errorCode, qint64 streamedMs)
{
    if (streamedMs < kMinStreamedMs) {
        return false;
    }
    switch (errorCode) {
    case kGraceful:
    case kUnexpectedEarlyEnd:
    case kProtectedContent:
    case kFrameConversion:
        return false;
    default:
        return true; // no video traffic, bad network, timeouts, socket errors
    }
}

}
