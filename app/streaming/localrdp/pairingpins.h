#pragma once

#include <QHash>
#include <QString>

// Pairing that is still running, per PC. Sunshine keeps a pairing request that
// waits for its PIN for 5 minutes and answers any new request from the same
// Moonlight with "already exists" (409) meanwhile. So a second click on the PC
// shows the PIN of the running pairing again instead of starting another one.

namespace LocalRdp {

class PairingPins {
public:
    static constexpr qint64 kTimeoutMs = 5 * 60 * 1000; // Sunshine's PAIRING_SESSION_TIMEOUT

    // The PIN of the pairing still running for this PC, or empty (none, it
    // finished, or Sunshine has forgotten it by now)
    QString pinFor(const QString& computer, qint64 nowMs) const;
    void started(const QString& computer, const QString& pin, qint64 nowMs);
    void finished(const QString& computer);

private:
    struct Pending {
        QString pin;
        qint64 startedMs = 0;
    };
    QHash<QString, Pending> m_Pending;
};

}
