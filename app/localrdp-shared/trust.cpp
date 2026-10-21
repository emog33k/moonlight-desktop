#include "trust.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dpapi.h>

namespace LocalRdp {

bool isPairedClient(const QByteArray& sunshineStateJson, const QSslCertificate& clientCert)
{
    if (clientCert.isNull()) {
        return false;
    }

    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(sunshineStateJson, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        return false;
    }

    const QJsonValue devices = doc.object().value("root").toObject().value("named_devices");
    if (!devices.isArray()) {
        return false;
    }

    for (const QJsonValue& device : devices.toArray()) {
        const QJsonObject d = device.toObject();
        if (d.value("enabled").toString() != QLatin1String("true")) {
            continue;
        }
        const QSslCertificate paired(d.value("cert").toString().toLatin1(), QSsl::Pem);
        if (!paired.isNull() && paired == clientCert) {
            return true;
        }
    }
    return false;
}

static std::optional<QByteArray> runDpapi(const QByteArray& input, bool protect)
{
    DATA_BLOB in;
    in.pbData = (BYTE*)input.constData();
    in.cbData = (DWORD)input.size();
    DATA_BLOB out = {};

    const BOOL ok = protect
        ? CryptProtectData(&in, L"LocalRDP", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)
        : CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out);
    if (!ok) {
        return std::nullopt;
    }
    QByteArray result((const char*)out.pbData, (int)out.cbData);
    SecureZeroMemory(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return result;
}

std::optional<QByteArray> protectForCurrentUser(const QByteArray& plain)
{
    return runDpapi(plain, true);
}

std::optional<QByteArray> unprotectForCurrentUser(const QByteArray& blob)
{
    return runDpapi(blob, false);
}

}
