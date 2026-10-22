#include "pairingpins.h"

namespace LocalRdp {

QString PairingPins::pinFor(const QString& computer, qint64 nowMs) const
{
    const auto it = m_Pending.constFind(computer);
    if (it == m_Pending.constEnd() || nowMs - it->startedMs >= kTimeoutMs) {
        return QString();
    }
    return it->pin;
}

void PairingPins::started(const QString& computer, const QString& pin, qint64 nowMs)
{
    m_Pending.insert(computer, Pending{ pin, nowMs });
}

void PairingPins::finished(const QString& computer)
{
    m_Pending.remove(computer);
}

}
