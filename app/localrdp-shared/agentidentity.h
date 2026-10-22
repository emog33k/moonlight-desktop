#pragma once

// The agent's own TLS identity.
//
// The agent must not hold Sunshine's private key (anyone who reads it can pose
// as Sunshine to the laptop). Instead the installer, running as administrator,
// makes a separate key for the agent and a certificate for it signed with
// Sunshine's key. The laptop, which pinned Sunshine's certificate at pairing,
// accepts a server that shows either that exact certificate or an agent
// certificate signed by it. A stolen agent key can then only pose as the agent.

#include <QByteArray>
#include <QSslCertificate>

#include <optional>

namespace LocalRdp {

struct AgentIdentity {
    QByteArray certPem;
    QByteArray keyPem; // PKCS#8, unencrypted (the caller protects it)
};

constexpr const char* kAgentCertCommonName = "LocalRDP Agent";

// Makes a new P-256 key and a certificate for it (CN "LocalRDP Agent", issuer =
// Sunshine's subject), signed with Sunshine's key. Empty on any failure.
std::optional<AgentIdentity> issueAgentIdentity(const QByteArray& sunshineCertPem, const QByteArray& sunshineKeyPem);

// The laptop's server check: `peer` is the pinned certificate itself, or a
// "LocalRDP Agent" certificate whose signature verifies with the pinned
// certificate's public key and whose issuer is the pinned subject.
bool isTrustedServerCertificate(const QSslCertificate& peer, const QSslCertificate& pinned);

}
