#pragma once

#include <QByteArray>
#include <QSslCertificate>

#include <optional>

namespace LocalRdp {

// True if clientCert belongs to a device that is paired with Sunshine and enabled,
// according to Sunshine's state file (root.named_devices[].cert / .enabled), by the
// same rule Sunshine uses. Anything malformed counts as "not paired".
bool isPairedClient(const QByteArray& sunshineStateJson, const QSslCertificate& clientCert);

// Windows DPAPI, scoped to the current user: only this Windows account can unprotect.
std::optional<QByteArray> protectForCurrentUser(const QByteArray& plain);
std::optional<QByteArray> unprotectForCurrentUser(const QByteArray& blob);

}
