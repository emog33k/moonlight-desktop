// LocalRDP Agent: runs in the user's session on the PC next to Sunshine and serves
// the paired Moonlight LocalRDP client (PC lock now; clipboard and files later).

#include "agentserver.h"
#include "displays_win32.h"
#include "localrdp-shared/trust.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
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
    static QMutex mutex; // the clipboard thread logs too
    QMutexLocker lock(&mutex);
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
    // Real pixel sizes for monitors and the cursor, whatever the scaling
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("LocalRDP Agent");

    // Diagnostics: print the monitor list the laptop would get, then exit
    if (app.arguments().contains("--list-displays")) {
        Proto::DisplayList list;
        list.displays = enumerateDisplays();
        const QByteArray json = Proto::encodeDisplayList(list) + '\n';
        fwrite(json.constData(), 1, (size_t)json.size(), stdout);
        fflush(stdout);
        return 0;
    }

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
    // --bind / --port: for local testing (e.g. --bind 127.0.0.1)
    const int bindArg = args.indexOf("--bind");
    if (bindArg >= 0 && bindArg + 1 < args.size()) {
        config.bindAddress = QHostAddress(args.at(bindArg + 1));
    }
    const int portArg = args.indexOf("--port");
    if (portArg >= 0 && portArg + 1 < args.size()) {
        config.port = (quint16)args.at(portArg + 1).toUInt();
    }
    config.serverCert = cert;
    config.serverKey = key;
    config.readSunshineState = [statePath]() {
        QFile f(statePath);
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    };
    config.lockWorkstation = []() { LockWorkStation(); };
    const QString confPath = QFileInfo(statePath).absolutePath() + "/sunshine.conf";
    config.listDisplays = [confPath]() {
        Proto::DisplayList list;
        list.displays = enumerateDisplays();
        QFile conf(confPath);
        list.initial = conf.open(QIODevice::ReadOnly) ? Proto::initialDisplayFromConfig(list.displays, conf.readAll()) : -1;
        return list;
    };
    config.onPeerOffer = [](const Clip::Offer& offer) {
        writeLog(QString("laptop clipboard: %1%2%3%4 file(s)")
                     .arg(offer.textBytes ? "text, " : "")
                     .arg(offer.imageBytes ? "image, " : "")
                     .arg(offer.pngBytes ? "png, " : "")
                     .arg(offer.files.size()));
    };

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
