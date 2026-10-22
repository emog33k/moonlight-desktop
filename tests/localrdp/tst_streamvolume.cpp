#include "tests.h"
#include "streaming/localrdp/streamvolume.h"

#include <QTest>

#include <vector>

using namespace LocalRdp;

void TestStreamVolume::floatScaled()
{
    std::vector<float> s = { 1.0f, -0.5f, 0.25f, 0.0f };
    applyStreamVolume(s.data(), (int)s.size(), 50);
    QCOMPARE(s[0], 0.5f);
    QCOMPARE(s[1], -0.25f);
    QCOMPARE(s[2], 0.125f);
    QCOMPARE(s[3], 0.0f);
}

void TestStreamVolume::shortScaled()
{
    std::vector<short> s = { 32767, -32768, 1000, -1000 };
    applyStreamVolume(s.data(), (int)s.size(), 50);
    QCOMPARE((int)s[0], 16383);
    QCOMPARE((int)s[1], -16384);
    QCOMPARE((int)s[2], 500);
    QCOMPARE((int)s[3], -500);
}

void TestStreamVolume::fullAndZero()
{
    std::vector<short> s = { 1234, -4321 };
    applyStreamVolume(s.data(), (int)s.size(), 100); // untouched
    QCOMPARE((int)s[0], 1234);
    applyStreamVolume(s.data(), (int)s.size(), 0);
    QCOMPARE((int)s[0], 0);
    QCOMPARE((int)s[1], 0);
    std::vector<float> f = { 0.7f };
    applyStreamVolume(f.data(), 1, 250); // out of range: treated as 100
    QCOMPARE(f[0], 0.7f);
}

void TestStreamVolume::sharedSetting()
{
    setStreamAudio(false, 40);
    QCOMPARE(streamAudioOn(), false);
    QCOMPARE(streamVolume(), 40);
    setStreamAudio(true, 130);
    QCOMPARE(streamAudioOn(), true);
    QCOMPARE(streamVolume(), 100); // clamped
    setStreamAudio(true, -5);
    QCOMPARE(streamVolume(), 0);
    setStreamAudio(true, 100);
}
