pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

MenuBar {
    id: control

    property color menuBarColor: "#171e2a"
    property color menuBarBorderColor: "#40516a"
    property color textColor: "#f3f6fb"
    property color mutedTextColor: "#93a2ba"
    property color accentColor: "#285da9"

    delegate: MenuBarItem {
        id: topLevelItem

        leftPadding: 12
        rightPadding: 12
        topPadding: 7
        bottomPadding: 7

        contentItem: Text {
            text: topLevelItem.text
            color: topLevelItem.enabled ? control.textColor : control.mutedTextColor
            font.pixelSize: 12
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }

        background: Rectangle {
            radius: 3
            color: topLevelItem.highlighted || topLevelItem.down || topLevelItem.activeFocus ? control.accentColor : "transparent"
            border.width: topLevelItem.activeFocus ? 1 : 0
            border.color: "#72a7fa"
        }
    }

    background: Rectangle {
        color: control.menuBarColor

        Rectangle {
            height: 1
            color: control.menuBarBorderColor
            anchors {
                bottom: parent.bottom
                left: parent.left
                right: parent.right
            }
        }
    }
}
