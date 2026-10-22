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
    bool keepOpen = false;              // choosing it leaves the menu open (e.g. volume steps)
};

// The toolbar's buttons, left to right; Session holds the right-hand buttons.
enum class MenuSection { Mouse, Keyboard, Clipboard, Screen, Sound, Actions, Pc, Session };

// Quality in one click (bitrate and frame rate; the resolution stays).
struct QualityPreset {
    QString name;
    int kbps;
    int fps;
};
QVector<QualityPreset> qualityPresets();

// The number Windows shows for a monitor (Settings > Display), from its device
// name "\\.\DISPLAYn"; position + 1 if the name has none.
int windowsDisplayNumber(const QString& deviceName, int position);

// Windows virtual-key codes for a quick key combination, modifiers first
// (pressed in this order, released in reverse). Empty for an unknown one.
QVector<short> quickKeyCombo(QuickKeys keys);

// One section's items.
QVector<MenuItem> buildSectionMenu(MenuSection section, const StreamState& state);

// What a toolbar button opens: its section; "ПК" also ends with the session
// items (minimize, disconnect, disconnect and close), as there is no separate
// session dropdown.
QVector<MenuItem> buildToolbarMenu(MenuSection section, const StreamState& state);

// Every item of every section in one list (the complete set, e.g. for checks).
QVector<MenuItem> buildStreamMenu(const StreamState& state);

}
