#include "tests.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTest>

#include <cstdio>

// Runs one test class, writing its log to <exe dir>/results/<class>.txt and echoing
// it to stdout (QtTest's own console output is lost in some Windows shells).
static int runTest(QObject* test)
{
    const QString dir = QCoreApplication::applicationDirPath() + "/results";
    QDir().mkpath(dir);
    const QString file = dir + "/" + test->metaObject()->className() + ".txt";

    QStringList args = { QCoreApplication::applicationFilePath(), "-o", file + ",txt" };
    const int failures = QTest::qExec(test, args);

    QFile log(file);
    if (log.open(QIODevice::ReadOnly)) {
        fputs(log.readAll().constData(), stdout);
        fflush(stdout);
    }
    return failures;
}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName("LocalRDP Tests");
    QCoreApplication::setApplicationName("localrdp_tests");

    int failures = 0;
    {
        TestStreamingPreferences t;
        failures += runTest(&t);
    }
    {
        TestStreamMenuModel t;
        failures += runTest(&t);
    }
    {
        TestMenuLayout t;
        failures += runTest(&t);
    }
    {
        TestProtocol t;
        failures += runTest(&t);
    }
    {
        TestTrust t;
        failures += runTest(&t);
    }
    {
        TestPeerConnection t;
        failures += runTest(&t);
    }
    {
        TestAgentServer t;
        failures += runTest(&t);
    }
    {
        TestAgentClient t;
        failures += runTest(&t);
    }
    {
        TestSessionMenuHost t;
        failures += runTest(&t);
    }
    {
        TestClipMessages t;
        failures += runTest(&t);
    }
    {
        TestClipExchange t;
        failures += runTest(&t);
    }
    {
        TestClipReader t;
        failures += runTest(&t);
    }
    {
        TestRemoteDataObject t;
        failures += runTest(&t);
    }
    {
        TestClipboardBridge t;
        failures += runTest(&t);
    }
    {
        TestVideoArea t;
        failures += runTest(&t);
    }
    {
        TestToolbarLayout t;
        failures += runTest(&t);
    }
    {
        TestHostDisplays t;
        failures += runTest(&t);
    }
    {
        TestCursorShape t;
        failures += runTest(&t);
    }
    {
        TestCursorSync t;
        failures += runTest(&t);
    }
    {
        TestAgentIdentity t;
        failures += runTest(&t);
    }
    {
        TestScreenNudge t;
        failures += runTest(&t);
    }
    {
        TestStreamVolume t;
        failures += runTest(&t);
    }
    {
        TestAgentNotices t;
        failures += runTest(&t);
    }
    {
        TestWindowMemory t;
        failures += runTest(&t);
    }
    {
        TestReconnect t;
        failures += runTest(&t);
    }
    std::printf("LOCALRDP TESTS: %s\n", failures == 0 ? "ALL PASSED" : "FAILED");
    return failures == 0 ? 0 : 1;
}
