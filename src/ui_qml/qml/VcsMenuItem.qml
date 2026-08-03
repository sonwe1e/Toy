pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

// Dark styled menu row laid out as three fixed columns: [Check/Icon] Label ... Shortcut / ›.
// Used inside VcsMenu popups (and VcsMenuBar top-level menus).
MenuItem {
    id: control

    property bool popupInputContext: true
    property string shortcutText: ""
    property color textColor: "#f3f6fb"
    property color disabledTextColor: "#637086"
    property color shortcutColor: "#93a2ba"
    property color hoverColor: "#285da9"
    property color checkColor: "#4b8df8"
    property color checkBackgroundColor: "#202938"
    property color checkBorderColor: "#40516a"
    readonly property var nestedMenu: control.subMenu

    visible: !control.nestedMenu || control.nestedMenu.menuItemVisible
    enabled: !control.nestedMenu || control.nestedMenu.menuItemEnabled

    implicitHeight: 35
    leftPadding: control.checkable ? 30 : 12
    rightPadding: 12
    topPadding: 0
    bottomPadding: 0

    // The native submenu arrow is rendered inside contentItem so the row keeps an exact
    // three-column layout; reserve nothing on the right for the internal arrow.
    arrow: Item {
        visible: false
        implicitWidth: 0
        implicitHeight: 0
    }

    contentItem: Item {
        Text {
            id: labelText

            anchors {
                left: parent.left
                right: trailingText.left
                rightMargin: 10
                verticalCenter: parent.verticalCenter
            }
            text: control.text
            color: control.enabled ? control.textColor : control.disabledTextColor
            font.pixelSize: 13
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        VcsMenuShortcut {
            id: trailingText

            anchors {
                right: parent.right
                verticalCenter: parent.verticalCenter
            }
            shortcutEnabled: control.enabled
            text: control.subMenu ? "›" : control.shortcutText
            shortcutColor: control.subMenu ? control.textColor : control.shortcutColor
            font.pixelSize: control.subMenu ? 14 : 12
            font.bold: control.subMenu
        }
    }

    indicator: Rectangle {
        visible: control.checkable
        x: 8
        y: control.height / 2 - height / 2
        implicitWidth: 14
        implicitHeight: 14
        radius: 3
        color: control.checked ? control.checkColor : control.checkBackgroundColor
        border.color: control.checked ? control.checkColor : control.checkBorderColor
        border.width: 1

        Text {
            anchors.centerIn: parent
            text: control.checked ? "✓" : ""
            color: "#ffffff"
            font.pixelSize: 9
            font.bold: true
        }
    }

    background: Rectangle {
        radius: 3
        color: control.hovered || control.highlighted ? control.hoverColor : "transparent"
    }
}
