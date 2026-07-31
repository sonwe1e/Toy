pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

SpinBox {
    id: control

    property color textColor: "#f3f6fb"
    property color mutedTextColor: "#93a2ba"
    property color accentColor: "#4b8df8"
    property color panelColor: "#1d2635"
    property color borderColor: "#303d51"

    from: -16
    to: 16
    editable: true
    implicitWidth: 76
    implicitHeight: 34

    contentItem: TextInput {
        text: control.textFromValue(control.value, control.locale)
        color: control.enabled ? control.textColor : control.mutedTextColor
        selectionColor: control.accentColor
        selectedTextColor: "white"
        horizontalAlignment: TextInput.AlignHCenter
        verticalAlignment: TextInput.AlignVCenter
        readOnly: !control.editable
        validator: control.validator
        inputMethodHints: Qt.ImhFormattedNumbersOnly
    }

    background: Rectangle {
        radius: 4
        color: control.panelColor
        border.color: control.activeFocus ? control.accentColor : control.borderColor
    }
}
