#include "tests.h"
#include "streaming/localrdp/reconnect.h"

#include <QTest>

using namespace LocalRdp;

void TestReconnect::networkLossReconnects()
{
    QVERIFY(shouldAutoReconnect(-100, 60000));    // video stopped coming (Wi-Fi gone)
    QVERIFY(shouldAutoReconnect(-101, 60000));    // network too bad for a while
    QVERIFY(shouldAutoReconnect(-1, 60000));      // connection timed out
    QVERIFY(shouldAutoReconnect(0x0000274C, 60000)); // a socket error code
}

void TestReconnect::hostSideEndsDoNot()
{
    QVERIFY(!shouldAutoReconnect(0, 60000));      // the PC ended the stream (app closed there)
    QVERIFY(!shouldAutoReconnect(-102, 60000));   // something failed on the PC
    QVERIFY(!shouldAutoReconnect(-103, 60000));   // protected content
    QVERIFY(!shouldAutoReconnect(-104, 60000));   // encoder failure
}

void TestReconnect::onlyAfterItWasRunning()
{
    // A stream that never really worked shows its error as before
    QVERIFY(!shouldAutoReconnect(-100, 2000));
    QVERIFY(shouldAutoReconnect(-100, kMinStreamedMs));
}
