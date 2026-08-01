pragma ComponentBehavior: Bound

import QtQuick

// Renders the trailing shortcut column of a VcsMenuItem row, or the submenu "›" arrow.
Text {
    id: control

    property color shortcutColor: "#93a2ba"
    property color disabledColor: "#637086"
    property bool shortcutEnabled: true

    color: control.shortcutEnabled ? control.shortcutColor : control.disabledColor
    font.pixelSize: 12
    verticalAlignment: Text.AlignVCenter
    horizontalAlignment: Text.AlignRight
}
