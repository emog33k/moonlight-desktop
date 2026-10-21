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
};

// Builds the in-stream menu, top to bottom, reflecting the current state.
QVector<MenuItem> buildStreamMenu(const StreamState& state);

}
