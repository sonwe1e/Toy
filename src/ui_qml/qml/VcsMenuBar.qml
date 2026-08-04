pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import "VcsTheme.js" as Theme

MenuBar {
    id: control

    property color menuBarColor: Theme.menu
    property color menuBarBorderColor: Theme.menuBorder
    property color textColor: Theme.primaryText
    property color mutedTextColor: Theme.mutedText
    property color accentColor: Theme.controlPressed

    delegate: MenuBarItem {
        id: topLevelItem

        objectName: menu && menu.objectName.length > 0 ? menu.objectName + "Button" : ""
        enabled: Boolean(menu && menu.enabled)

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
            color: topLevelItem.enabled && (topLevelItem.highlighted || topLevelItem.down || topLevelItem.activeFocus) ? control.accentColor : "transparent"
            border.width: topLevelItem.activeFocus ? 1 : 0
            border.color: Theme.focus
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
