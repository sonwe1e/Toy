pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import "VcsTheme.js" as Theme

ToolButton {
    id: control

    property string helpText: ""
    property url iconSource: ""
    property int iconExtent: 20
    property int labelPixelSize: 16
    property real controlRadius: 6
    property bool prominent: false
    property int toolTipDelay: 500

    implicitWidth: 34
    implicitHeight: 34
    padding: 0
    activeFocusOnTab: true
    Accessible.name: helpText.length > 0 ? helpText.split("\n")[0] : text
    Accessible.description: helpText

    contentItem: Item {
        Image {
            anchors.centerIn: parent
            visible: control.iconSource.toString().length > 0
            source: control.iconSource
            sourceSize.width: control.iconExtent
            sourceSize.height: control.iconExtent
            fillMode: Image.PreserveAspectFit
            opacity: control.enabled ? 1.0 : 0.35
        }

        Text {
            anchors.fill: parent
            visible: control.iconSource.toString().length === 0
            text: control.text
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            color: control.enabled ? Theme.primaryText : Theme.disabledText
            font.pixelSize: control.labelPixelSize
            font.weight: Font.DemiBold
        }
    }

    background: Rectangle {
        radius: control.controlRadius
        color: !control.enabled ? Theme.disabledPanel : (control.down ? (control.prominent ? Theme.accentPressed : Theme.controlPressed) : (control.hovered ? (control.prominent ? Theme.accentHover : Theme.controlHover) : (control.prominent ? Theme.accent : Theme.control)))
        border.width: control.activeFocus ? 2 : 1
        border.color: control.activeFocus ? (control.prominent ? Theme.strongFocus : Theme.accent) : (control.enabled ? (control.prominent ? Theme.focus : Theme.controlBorder) : Theme.disabledBorder)
    }

    VcsToolTip {
        visible: control.hovered && control.helpText.length > 0
        text: control.helpText
        delay: control.toolTipDelay
    }
}
