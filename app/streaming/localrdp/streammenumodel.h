#pragma once

#include "streamactions.h"

#include <QString>
#include <QVector>

#include <optional>

namespace LocalRdp {

enum class MenuItemKind { Header, Radio, Check, Command, Separator };

struct MenuItem {
    MenuItemKind kind;
    QString label;
    QString shortcut;                   // empty if the action has no shortcut
    std::optional<StreamAction> action; // empty for headers and separators
    bool checked = false;
};

// Builds the in-stream menu, top to bottom, reflecting the current state.
QVector<MenuItem> buildStreamMenu(const StreamState& state);

}
