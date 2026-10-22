#include "tests.h"
#include "streaming/localrdp/pairingpins.h"

#include <QTest>

using LocalRdp::PairingPins;

void TestPairingPins::secondClickShowsSamePin()
{
    PairingPins p;
    QVERIFY(p.pinFor("pc1", 0).isEmpty());
    p.started("pc1", "1505", 1000);
    QCOMPARE(p.pinFor("pc1", 2000), QString("1505"));
    QCOMPARE(p.pinFor("pc1", 1000 + PairingPins::kTimeoutMs - 1), QString("1505"));
}

void TestPairingPins::finishedAllowsNewPairing()
{
    PairingPins p;
    p.started("pc1", "1505", 1000);
    p.finished("pc1"); // paired, or failed (wrong PIN, PC gone...)
    QVERIFY(p.pinFor("pc1", 2000).isEmpty());
}

void TestPairingPins::sunshineForgetsAfterTimeout()
{
    PairingPins p;
    p.started("pc1", "1505", 1000);
    QVERIFY(p.pinFor("pc1", 1000 + PairingPins::kTimeoutMs).isEmpty());
}

void TestPairingPins::perComputer()
{
    PairingPins p;
    p.started("pc1", "1505", 0);
    p.started("pc2", "4242", 0);
    QCOMPARE(p.pinFor("pc1", 1), QString("1505"));
    QCOMPARE(p.pinFor("pc2", 1), QString("4242"));
    p.finished("pc1");
    QVERIFY(p.pinFor("pc1", 1).isEmpty());
    QCOMPARE(p.pinFor("pc2", 1), QString("4242"));
    QVERIFY(p.pinFor("pc3", 1).isEmpty());
}
