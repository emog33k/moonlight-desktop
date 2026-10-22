// LocalRDP Agent: runs in the user's session on the PC next to Sunshine and serves
// the paired Moonlight LocalRDP client (PC lock, clipboard and files, the PC's
// monitors and cursor).

#include "agentserver.h"
#include "cursorwatch_win32.h"
#include "displays_win32.h"
#include "localrdp-shared/agentidentity.h"
#include "localrdp-shared/trust.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QSaveFile>
#include <QSslKey>
#include <QTimer>

#include <cstdio>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>
#include <wtsapi32.h>

using namespace LocalRdp;

static QString s_LogPath;

// Log lines may contain text from the network (e.g. a rejected certificate's
// name): no control or direction-changing characters, bounded length.
static QString cleanForLog(const QString& text)
{
    QString out;
    out.reserve(qMin(text.size(), 500));
    for (QChar c : text) {
        if (out.size() >= 500) {
            out += QStringLiteral("…");
            break;
        }
        const QChar::Category cat = c.category();
        out += (c.unicode() < 0x20 || cat == QChar::Other_Format || cat == QChar::Other_Control) ? QChar('?') : c;
    }
    return out;
}

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
        log.write((QDateTime::currentDateTime().toString(Qt::ISODate) + " " + cleanForLog(text) + "\n").toUtf8());
    }
}

// Process id(s) of the running Sunshine. A restarted Sunshine gets a new one
// (and shows its cursor in the video again); the start time is not readable
// for a normal user, the id is.
static QString sunshineInstance()
{
    QStringList pids;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return QString();
    }
    PROCESSENTRY32W entry = { sizeof(entry) };
    for (BOOL ok = Process32FirstW(snapshot, &entry); ok; ok = Process32NextW(snapshot, &entry)) {
        if (_wcsicmp(entry.szExeFile, L"sunshine.exe") == 0) {
            pids << QString::number(entry.th32ProcessID);
        }
    }
    CloseHandle(snapshot);
    pids.sort();
    return pids.join(',');
}

// Whether this Windows session is locked. Unknown counts as locked, so the
// clipboard is never shared by mistake.
static bool sessionLocked()
{
    LPWSTR buffer = nullptr;
    DWORD bytes = 0;
    if (!WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, WTS_CURRENT_SESSION, WTSSessionInfoEx, &buffer, &bytes) ||
            buffer == nullptr) {
        return true;
    }
    const auto info = (const WTSINFOEXW*)buffer;
    const bool locked = bytes < sizeof(WTSINFOEXW) || info->Level != 1 ||
                        info->Data.WTSInfoExLevel1.SessionFlags != WTS_SESSIONSTATE_UNLOCK;
    WTSFreeMemory(buffer);
    return locked;
}

static QString programDataDir()
{
    return QDir::fromNativeSeparators(qEnvironmentVariable("ProgramData", "C:/ProgramData")) + "/LocalRDP Agent";
}

static QString sunshineConfigDir()
{
    return QDir::fromNativeSeparators(qEnvironmentVariable("ProgramFiles", "C:/Program Files")) + "/Sunshine/config";
}

static QString argValue(const QStringList& args, const QString& name, const QString& fallback = QString())
{
    const int i = args.indexOf(name);
    return (i >= 0 && i + 1 < args.size()) ? args.at(i + 1) : fallback;
}

static void printLine(const QString& text)
{
    const QByteArray line = text.toUtf8() + '\n';
    fwrite(line.constData(), 1, (size_t)line.size(), stdout);
    fflush(stdout);
}

static bool writeFile(const QString& path, const QByteArray& data)
{
    QSaveFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(data) == data.size() && f.commit();
}

// Installer step (run elevated, as the same Windows user who runs the agent):
// make the agent's own key and certificate, signed with Sunshine's key, which
// stays where it is (readable by administrators only).
static int issueIdentity(const QString& credentialsDir, const QString& outDir)
{
    QFile certFile(credentialsDir + "/cacert.pem");
    QFile keyFile(credentialsDir + "/cakey.pem");
    if (!certFile.open(QIODevice::ReadOnly) || !keyFile.open(QIODevice::ReadOnly)) {
        printLine("cannot read Sunshine's certificate/key in " + credentialsDir);
        return 1;
    }
    QByteArray sunshineKey = keyFile.readAll();
    const auto id = issueAgentIdentity(certFile.readAll(), sunshineKey);
    sunshineKey.fill('\0');
    if (!id) {
        printLine("could not make the agent certificate");
        return 1;
    }
    QByteArray keyPem = id->keyPem;
    const auto blob = protectForCurrentUser(keyPem);
    keyPem.fill('\0');
    if (!blob || !writeFile(outDir + "/agent.crt", id->certPem) || !writeFile(outDir + "/agent.key.dpapi", *blob)) {
        printLine("could not write the agent certificate/key to " + outDir);
        return 1;
    }
    // Older versions kept a copy of Sunshine's own key here
    QFile::remove(outDir + "/server.key.dpapi");
    QFile::remove(outDir + "/server.crt");
    printLine("agent certificate written to " + outDir);
    return 0;
}

int main(int argc, char* argv[])
{
    // OpenSSL would otherwise look for its config and modules in the folder it
    // was built in (a path on drive D: of the build machine) that any user
    // could create here.
    wchar_t exePath[MAX_PATH * 2] = {};
    GetModuleFileNameW(nullptr, exePath, (DWORD)(sizeof(exePath) / sizeof(exePath[0])) - 1);
    const QString appDir = QDir::toNativeSeparators(QFileInfo(QString::fromWCharArray(exePath)).absolutePath());
    qputenv("OPENSSL_CONF", (appDir + "\\openssl-none.cnf").toLocal8Bit());
    qputenv("OPENSSL_MODULES", appDir.toLocal8Bit());
    qputenv("OPENSSL_ENGINES", appDir.toLocal8Bit());

    // Real pixel sizes for monitors and the cursor, whatever the scaling
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("LocalRDP Agent");
    const QStringList args = app.arguments();

    // Diagnostics: print the monitor list the laptop would get, then exit
    if (args.contains("--list-displays")) {
        Proto::DisplayList list;
        list.displays = enumerateDisplays();
        printLine(QString::fromUtf8(Proto::encodeDisplayList(list)));
        return 0;
    }
    if (args.contains("--issue-identity")) {
        return issueIdentity(argValue(args, "--credentials", sunshineConfigDir() + "/credentials"),
                             argValue(args, "--out", programDataDir()));
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

    const QString statePath = argValue(args, "--sunshine-state", sunshineConfigDir() + "/sunshine_state.json");

    // The agent's own certificate (signed with Sunshine's key by the installer)
    // and its key, DPAPI-protected for this Windows user.
    QFile certFile(programDataDir() + "/agent.crt");
    QFile keyFile(programDataDir() + "/agent.key.dpapi");
    if (!certFile.open(QIODevice::ReadOnly) || !keyFile.open(QIODevice::ReadOnly)) {
        writeLog("missing agent certificate/key in " + programDataDir() + "; run the installer again");
        return 1;
    }
    const QSslCertificate cert(certFile.readAll(), QSsl::Pem);
    auto keyPem = unprotectForCurrentUser(keyFile.readAll());
    if (cert.isNull() || !keyPem) {
        writeLog("cannot read the agent certificate/key (wrong Windows user?); run the installer again");
        return 1;
    }
    const QSslKey key(*keyPem, QSsl::Ec, QSsl::Pem);
    keyPem->fill('\0');
    if (key.isNull()) {
        writeLog("agent key has an unsupported format; run the installer again");
        return 1;
    }

    AgentConfig config;
    // --bind / --port: for local testing (e.g. --bind 127.0.0.1)
    if (args.contains("--bind")) {
        config.bindAddress = QHostAddress(argValue(args, "--bind"));
    }
    if (args.contains("--port")) {
        config.port = (quint16)argValue(args, "--port").toUInt();
    }
    config.serverCert = cert;
    config.serverKey = key;
    config.readSunshineState = [statePath]() {
        QFile f(statePath);
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    };
    config.lockWorkstation = []() { LockWorkStation(); };
    config.isSessionLocked = &sessionLocked;
    CursorWatcher cursorWatcher;
    config.readCursor = [&cursorWatcher]() { return cursorWatcher.read(); };
    config.sunshineInstance = &sunshineInstance;
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

    // The port may be taken for a while (e.g. right after a restart): keep trying
    QTimer listenRetry;
    listenRetry.setInterval(5000);
    QObject::connect(&listenRetry, &QTimer::timeout, [&server, &listenRetry]() {
        if (server.listen()) {
            listenRetry.stop();
        }
    });
    if (!server.listen()) {
        listenRetry.start();
    }
    writeLog("LocalRDP Agent started");

    const int rc = app.exec();
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return rc;
}
