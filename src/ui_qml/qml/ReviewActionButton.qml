pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

Button {
    id: control

    property bool blocksGlobalMediaShortcuts: true
    property string helpText: ""
    property bool prominent: false
    property color textColor: "#f3f6fb"
    property color accentColor: "#4b8df8"

    implicitWidth: Math.max(112, contentItem.implicitWidth + 34)
    implicitHeight: 40
    leftPadding: 17
    rightPadding: 17
    activeFocusOnTab: true

    contentItem: Text {
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        text: control.text
        color: control.enabled ? control.textColor : "#637086"
        font.pixelSize: 13
        font.weight: Font.DemiBold
        elide: Text.ElideRight
    }

    background: Rectangle {
        radius: 5
        color: !control.enabled ? "#202938" : (control.down ? (control.prominent ? "#2662bd" : "#285da9") : (control.hovered ? (control.prominent ? "#4f94ff" : "#2d69bf") : (control.prominent ? control.accentColor : "#253247")))
        border.width: control.activeFocus ? 2 : 1
        border.color: control.activeFocus ? (control.prominent ? "#b7d3ff" : control.accentColor) : (control.enabled ? (control.prominent ? "#72a7fa" : "#3b4d67") : "#2a3444")
    }

    ToolTip.visible: hovered && helpText.length > 0
    ToolTip.delay: 500
    ToolTip.text: helpText
}
