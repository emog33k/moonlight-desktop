#pragma once

#include "streamactions.h"

#include <QString>
#include <QVector>

#include <optional>

namespace LocalRdp {

// Status: a line of text (not clickable).
enum class MenuItemKind { Header, Radio, Check, Command, Separator, Status };

struct MenuItem {
    MenuItemKind kind;
    QString label;
    QString shortcut;                   // empty if the action has no shortcut
    std::optional<StreamAction> action; // empty for headers and separators
    bool checked = false;
    bool enabled = true;                // disabled items are grey and do nothing
    int param = 0;                      // e.g. display index for SwitchDisplay
};

// The toolbar's buttons, left to right; Session holds the right-hand buttons.
enum class MenuSection { Mouse, Keyboard, Clipboard, Screen, Pc, Session };

// One toolbar button's dropdown.
QVector<MenuItem> buildSectionMenu(MenuSection section, const StreamState& state);

// Everything in one menu (Ctrl+Alt+Shift+Space, e.g. in full screen).
QVector<MenuItem> buildStreamMenu(const StreamState& state);

}
