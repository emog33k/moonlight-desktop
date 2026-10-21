#pragma once

#include "clipexchange.h"
#include "clipmessages.h"

#include <memory>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objidl.h>

namespace LocalRdp {
namespace Clip {

// The peer's clipboard as a COM data object (what OleSetClipboard puts on our
// clipboard). Nothing is transferred until an application asks for a format:
// text/images are fetched whole, files are streamed (virtual files) in
// kClipChunk pieces while Explorer copies them. Returned with refcount 1.
IDataObject* createRemoteDataObject(const Offer& offer, std::shared_ptr<RemoteFetcher> fetcher);

}
}
