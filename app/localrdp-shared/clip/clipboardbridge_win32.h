#pragma once

#include "clipexchange.h"
#include "clipreader_win32.h"
#include "localrdp-shared/protocol.h"

#include <QStringList>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objidl.h>

namespace LocalRdp {
namespace Clip {

// Clipboard sync for one side. Runs its own thread (OLE STA with a hidden message
// window), so neither the stream loop nor the network thread ever waits on the
// clipboard. Local clipboard changes are announced to the peer (ClipOffer); the
// peer's offers are put on our clipboard as a lazy data object. File reads for the
// peer run on a separate I/O thread (OneDrive placeholders can take seconds).
class ClipboardBridge
{
public:
    // send: puts a frame on the connection; called from the bridge's threads.
    // useSystemClipboard = false: never touch the real clipboard (tests).
    explicit ClipboardBridge(SendFn send, bool useSystemClipboard = true);
    ~ClipboardBridge();

    ClipboardBridge(const ClipboardBridge&) = delete;
    ClipboardBridge& operator=(const ClipboardBridge&) = delete;

    // Off: our clipboard is no longer offered or served (the peer is told to drop
    // it) and the peer's offers are ignored. On again: announced like at connect.
    void setEnabled(bool enabled);
    void connectionUp();   // announce the current clipboard
    void connectionDown(); // forget both sides' offers

    // When the current local clipboard was copied (ms since 1970; 0 = unknown).
    // At connection start the newer clipboard wins (see Offer::initial). Set it
    // before connectionUp; it then follows local copies and accepted offers.
    void setLocalCopyTime(qint64 msSinceEpoch);
    qint64 localCopyTime() const { return m_LocalCopiedAt; }
    // Same copy time on both sides: true = take the peer's (the agent sets this,
    // so the laptop's clipboard wins ties).
    void setPreferPeerOnTie(bool prefer) { m_PreferPeerOnTie = prefer; }

    // ClipOffer / ClipRequest / ClipData / ClipError from the network thread.
    void onFrame(const Proto::Frame& frame);

    // "Send files to PC" and drag and drop: offer these as if they were copied.
    // done(ok, reason) is called on the bridge thread with the outcome.
    void offerFiles(const QStringList& paths, std::function<void(bool, QString)> done = {});

    // Called (on the clipboard thread) for every offer accepted from the peer: logs, tests.
    void setPeerOfferObserver(std::function<void(const Offer&)> observer);

private:
    void threadMain();
    void ioThreadMain();
    void post(std::function<void()> work);
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    // Clipboard thread only
    void announceLocalClipboard();
    void acceptPeerOffer(const Offer& offer);
    void onRetryTimer();
    void onHeartbeatTimer();
    void dropOurObject();
    void dropOurObjectReference();
    void sendSnapshot(const LocalSnapshot& snapshot, bool initial);

    SendFn m_Send;
    const bool m_UseSystemClipboard;
    std::atomic<bool> m_Enabled { true };
    std::atomic<bool> m_Connected { false };
    std::atomic<qint64> m_LocalCopiedAt { 0 };
    std::atomic<bool> m_PreferPeerOnTie { false };
    bool m_LocalHasContent = true;      // clipboard thread: last announce had something to offer
    std::function<void(const Offer&)> m_PeerOfferObserver; // clipboard thread

    LocalClipSource m_Source;
    std::shared_ptr<RemoteFetcher> m_Fetcher;
    quint64 m_NextSeq;                  // clipboard thread; random start (see constructor)
    IDataObject* m_OurObject = nullptr; // clipboard thread: the peer's data on our clipboard

    // Retries while another program holds the clipboard (clipboard thread)
    int m_AnnounceRetries = 0;
    bool m_AnnouncePending = false;
    bool m_AnnounceInitial = false;     // the pending announce is the connection-start one
    std::optional<Offer> m_PendingPeerOffer;
    int m_PeerOfferRetries = 0;
    bool m_DropPending = false;
    int m_DropRetries = 0;

    std::thread m_Thread;
    DWORD m_ThreadId = 0;
    HWND m_Hwnd = nullptr;
    std::mutex m_Mutex;
    std::condition_variable m_Ready;
    std::deque<std::function<void()>> m_Queue;
    bool m_Started = false;

    // File requests from the peer, served off the network thread
    std::thread m_IoThread;
    std::mutex m_IoMutex;
    std::condition_variable m_IoCv;
    std::deque<std::pair<Request, quint64>> m_IoQueue; // with the epoch it arrived in
    bool m_IoStop = false;
    std::atomic<quint32> m_IoBusyId { 0 };   // request being read now (0 = none)
    std::atomic<quint64> m_IoBusyEpoch { 0 };
    std::atomic<qint64> m_IoBusySince { 0 };
    // Bumped when the connection goes down or sharing is turned off: replies to
    // requests from before are never sent (they would reach the next connection)
    std::atomic<quint64> m_Epoch { 1 };
    void stopServing(); // any thread
};

}
}
