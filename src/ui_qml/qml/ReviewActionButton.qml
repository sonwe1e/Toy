pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import "VcsTheme.js" as Theme

Button {
    id: control

    property bool blocksGlobalMediaShortcuts: true
    property string helpText: ""
    property bool prominent: false
    property color textColor: Theme.primaryText
    property color accentColor: Theme.accent

    implicitWidth: Math.max(112, contentItem.implicitWidth + 34)
    implicitHeight: 40
    leftPadding: 17
    rightPadding: 17
    activeFocusOnTab: true

    contentItem: Text {
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        text: control.text
        color: control.enabled ? control.textColor : Theme.disabledText
        font.pixelSize: 13
        font.weight: Font.DemiBold
        elide: Text.ElideRight
    }

    background: Rectangle {
        radius: 5
        color: !control.enabled ? Theme.disabledPanel : (control.down ? (control.prominent ? Theme.accentPressed : Theme.controlPressed) : (control.hovered ? (control.prominent ? Theme.accentHover : Theme.controlHover) : (control.prominent ? control.accentColor : Theme.control)))
        border.width: control.activeFocus ? 2 : 1
        border.color: control.activeFocus ? (control.prominent ? Theme.strongFocus : control.accentColor) : (control.enabled ? (control.prominent ? Theme.focus : Theme.controlBorder) : Theme.disabledBorder)
    }

    VcsToolTip {
        visible: control.hovered && control.helpText.length > 0
        text: control.helpText
    }
}
