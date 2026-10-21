// LocalRDP Agent: runs in the user's session on the PC next to Sunshine and serves
// the paired Moonlight LocalRDP client (PC lock now; clipboard and files later).

#include "agentserver.h"
#include "localrdp-shared/trust.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QSslKey>
#include <QStandardPaths>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

using namespace LocalRdp;

static QString s_LogPath;

static void writeLog(const QString& text)
{
    if (s_LogPath.isEmpty()) {
        return;
    }
    QFile log(s_LogPath);
    if (log.size() > 1024 * 1024) {
        QFile::remove(s_LogPath + ".1");
        QFile::rename(s_LogPath, s_LogPath + ".1");
    }
    if (log.open(QIODevice::Append | QIODevice::Text)) {
        log.write((QDateTime::currentDateTime().toString(Qt::ISODate) + " " + text + "\n").toUtf8());
    }
}

static QString programDataDir()
{
    return QDir::fromNativeSeparators(qEnvironmentVariable("ProgramData", "C:/ProgramData")) + "/LocalRDP Agent";
}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("LocalRDP Agent");

    const QString logDir = QDir::fromNativeSeparators(qEnvironmentVariable("LOCALAPPDATA")) + "/LocalRDP Agent";
    QDir().mkpath(logDir);
    s_LogPath = logDir + "/agent.log";

    // One agent per user session
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\LocalRDPAgent");
    if (mutex == nullptr || GetLastError() == ERROR_ALREADY_EXISTS) {
        writeLog("another agent is already running; exiting");
        return 0;
    }

    QString statePath = "C:/Program Files/Sunshine/config/sunshine_state.json";
    const QStringList args = app.arguments();
    const int stateArg = args.indexOf("--sunshine-state");
    if (stateArg >= 0 && stateArg + 1 < args.size()) {
        statePath = args.at(stateArg + 1);
    }

    // Sunshine's certificate and key, exported by the installer (key DPAPI-protected
    // for this Windows user). Moonlight already trusts exactly this certificate.
    QFile certFile(programDataDir() + "/server.crt");
    QFile keyFile(programDataDir() + "/server.key.dpapi");
    if (!certFile.open(QIODevice::ReadOnly) || !keyFile.open(QIODevice::ReadOnly)) {
        writeLog("missing server certificate/key in " + programDataDir() + "; run the installer again");
        return 1;
    }
    const QSslCertificate cert(certFile.readAll(), QSsl::Pem);
    const auto keyPem = unprotectForCurrentUser(keyFile.readAll());
    if (cert.isNull() || !keyPem) {
        writeLog("cannot read server certificate/key (wrong Windows user?); run the installer again");
        return 1;
    }
    QSslKey key(*keyPem, QSsl::Rsa, QSsl::Pem);
    if (key.isNull()) {
        key = QSslKey(*keyPem, QSsl::Ec, QSsl::Pem);
    }
    if (key.isNull()) {
        writeLog("server key has an unsupported format");
        return 1;
    }

    AgentConfig config;
    config.serverCert = cert;
    config.serverKey = key;
    config.readSunshineState = [statePath]() {
        QFile f(statePath);
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    };
    config.lockWorkstation = []() { LockWorkStation(); };

    AgentServer server(config);
    QObject::connect(&server, &AgentServer::message, &writeLog);
    if (!server.listen()) {
        return 1;
    }
    writeLog("LocalRDP Agent started");

    const int rc = app.exec();
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return rc;
}
