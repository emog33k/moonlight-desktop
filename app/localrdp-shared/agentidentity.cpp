#include "agentidentity.h"

#include <openssl/bio.h>
#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <memory>

namespace LocalRdp {

namespace {

struct Free {
    void operator()(X509* p) const { X509_free(p); }
    void operator()(EVP_PKEY* p) const { EVP_PKEY_free(p); }
    void operator()(BIO* p) const { BIO_free(p); }
    void operator()(BIGNUM* p) const { BN_free(p); }
    void operator()(X509_EXTENSION* p) const { X509_EXTENSION_free(p); }
};
template <typename T>
using Ptr = std::unique_ptr<T, Free>;

Ptr<BIO> memBio(const QByteArray& data)
{
    return Ptr<BIO>(BIO_new_mem_buf(data.constData(), (int)data.size()));
}

QByteArray bioContents(BIO* bio)
{
    char* data = nullptr;
    const long size = BIO_get_mem_data(bio, &data);
    return size > 0 ? QByteArray(data, (int)size) : QByteArray();
}

Ptr<X509> fromQt(const QSslCertificate& cert)
{
    const QByteArray der = cert.toDer();
    const unsigned char* p = (const unsigned char*)der.constData();
    return Ptr<X509>(der.isEmpty() ? nullptr : d2i_X509(nullptr, &p, (long)der.size()));
}

bool addExtension(X509* cert, X509* issuer, int nid, const char* value)
{
    X509V3_CTX ctx;
    X509V3_set_ctx_nodb(&ctx);
    X509V3_set_ctx(&ctx, issuer, cert, nullptr, nullptr, 0);
    Ptr<X509_EXTENSION> ext(X509V3_EXT_conf_nid(nullptr, &ctx, nid, value));
    return ext && X509_add_ext(cert, ext.get(), -1) == 1;
}

}

std::optional<AgentIdentity> issueAgentIdentity(const QByteArray& sunshineCertPem, const QByteArray& sunshineKeyPem)
{
    Ptr<BIO> certBio = memBio(sunshineCertPem);
    Ptr<BIO> keyBio = memBio(sunshineKeyPem);
    if (!certBio || !keyBio) {
        return std::nullopt;
    }
    Ptr<X509> ca(PEM_read_bio_X509(certBio.get(), nullptr, nullptr, nullptr));
    Ptr<EVP_PKEY> caKey(PEM_read_bio_PrivateKey(keyBio.get(), nullptr, nullptr, nullptr));
    if (!ca || !caKey || X509_check_private_key(ca.get(), caKey.get()) != 1) {
        return std::nullopt;
    }

    Ptr<EVP_PKEY> key(EVP_EC_gen("P-256"));
    Ptr<X509> cert(X509_new());
    Ptr<BIGNUM> serial(BN_new());
    if (!key || !cert || !serial) {
        return std::nullopt;
    }

    const long twentyYears = 20L * 365 * 24 * 3600;
    X509_NAME* subject = X509_get_subject_name(cert.get());
    if (X509_set_version(cert.get(), 2) != 1 ||
            BN_rand(serial.get(), 127, BN_RAND_TOP_ANY, BN_RAND_BOTTOM_ANY) != 1 ||
            !BN_to_ASN1_INTEGER(serial.get(), X509_get_serialNumber(cert.get())) ||
            !X509_gmtime_adj(X509_getm_notBefore(cert.get()), -24 * 3600) ||
            !X509_gmtime_adj(X509_getm_notAfter(cert.get()), twentyYears) ||
            X509_set_pubkey(cert.get(), key.get()) != 1 ||
            X509_NAME_add_entry_by_txt(subject, "CN", MBSTRING_ASC,
                                       (const unsigned char*)kAgentCertCommonName, -1, -1, 0) != 1 ||
            X509_set_issuer_name(cert.get(), X509_get_subject_name(ca.get())) != 1 ||
            !addExtension(cert.get(), ca.get(), NID_basic_constraints, "critical,CA:FALSE") ||
            !addExtension(cert.get(), ca.get(), NID_ext_key_usage, "serverAuth") ||
            X509_sign(cert.get(), caKey.get(), EVP_sha256()) <= 0) {
        return std::nullopt;
    }

    Ptr<BIO> certOut(BIO_new(BIO_s_mem()));
    Ptr<BIO> keyOut(BIO_new(BIO_s_mem()));
    if (!certOut || !keyOut || PEM_write_bio_X509(certOut.get(), cert.get()) != 1 ||
            PEM_write_bio_PrivateKey(keyOut.get(), key.get(), nullptr, nullptr, 0, nullptr, nullptr) != 1) {
        return std::nullopt;
    }
    AgentIdentity id;
    id.certPem = bioContents(certOut.get());
    id.keyPem = bioContents(keyOut.get());
    if (id.certPem.isEmpty() || id.keyPem.isEmpty()) {
        return std::nullopt;
    }
    return id;
}

bool isTrustedServerCertificate(const QSslCertificate& peer, const QSslCertificate& pinned)
{
    if (peer.isNull() || pinned.isNull()) {
        return false;
    }
    if (peer == pinned) {
        return true;
    }
    if (peer.subjectInfo(QSslCertificate::CommonName) != QStringList({ QString::fromLatin1(kAgentCertCommonName) })) {
        return false;
    }
    Ptr<X509> p = fromQt(peer);
    Ptr<X509> pin = fromQt(pinned);
    if (!p || !pin) {
        return false;
    }
    EVP_PKEY* pinKey = X509_get0_pubkey(pin.get());
    return pinKey != nullptr &&
           X509_NAME_cmp(X509_get_issuer_name(p.get()), X509_get_subject_name(pin.get())) == 0 &&
           X509_verify(p.get(), pinKey) == 1;
}

}
