#pragma once

#include "clipmessages.h"

#include <QStringList>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objidl.h>

namespace LocalRdp {
namespace Clip {

// Everything needed to answer the peer about one clipboard state.
struct LocalSnapshot {
    Offer offer;
    QByteArray text;       // UTF-16LE, no terminator
    QByteArray dib;        // CF_DIB
    QByteArray png;        // "PNG" format
    QStringList filePaths; // absolute path per offer.files entry; empty for folders
    bool excluded = false; // marked private (password managers): must not be offered
};

// Reads a data object (OleGetClipboard() in production). Folders are walked
// recursively; a folder always comes before its contents. Symbolic links and
// junctions are not followed. Too many/too large files drop the file part only.
LocalSnapshot readDataObject(IDataObject* data, quint64 seq);

}
}
