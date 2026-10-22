#pragma once

#include <QCoreApplication>
#include <QString>

namespace LocalRdp {

// Moonlight Desktop's own UI text. Written in English here; other languages are
// in app/languages/desktop_<language>.ts, loaded next to Moonlight's own
// translations. Pass string literals only: a test finds every call in the
// sources and checks that its text is translated.
// disambiguation: when the same English text needs different translations.
inline QString dtr(const char* text, const char* disambiguation = nullptr)
{
    return QCoreApplication::translate("MoonlightDesktop", text, disambiguation);
}

}
