#include "tests.h"
#include "streaming/localrdp/agentnotices.h"

#include <QTest>

using namespace LocalRdp;

void TestAgentNotices::firstConnectIsQuiet()
{
    AgentNotices n;
    QVERIFY(n.update(AgentStatus::Connecting, false).isEmpty());
    QVERIFY(n.update(AgentStatus::Connected, false).isEmpty());
    QVERIFY(n.update(AgentStatus::Connected, false).isEmpty());
}

void TestAgentNotices::lostAndBack()
{
    AgentNotices n;
    n.update(AgentStatus::Connected, false);
    QVERIFY(n.update(AgentStatus::Connecting, false).contains("потеряна"));
    QVERIFY(n.update(AgentStatus::NotFound, false).isEmpty()); // still lost: once is enough
    QVERIFY(n.update(AgentStatus::Connected, false).contains("снова"));
}

void TestAgentNotices::neverConnectedIsQuiet()
{
    // No agent on the PC at all: the status dot says so, no pop-ups
    AgentNotices n;
    QVERIFY(n.update(AgentStatus::NotFound, false).isEmpty());
    QVERIFY(n.update(AgentStatus::Connecting, false).isEmpty());
}

void TestAgentNotices::pcLockChanges()
{
    AgentNotices n;
    QVERIFY(n.update(AgentStatus::Connected, false).isEmpty());
    QVERIFY(n.update(AgentStatus::Connected, true).contains("заблокирован"));
    QVERIFY(n.update(AgentStatus::Connected, true).isEmpty());
    QVERIFY(n.update(AgentStatus::Connected, false).contains("разблокирован"));
    // Already locked when we connect: no "locked" pop-up (the lock screen shows it)
    AgentNotices m;
    QVERIFY(m.update(AgentStatus::Connected, true).isEmpty());
}
