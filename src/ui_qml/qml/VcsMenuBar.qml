pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

// Dark, compact top-level menu bar. The popup menus are styled by VcsMenu/VcsMenuItem; the bar
// itself only needs the same background so the four titles sit on the unified panel color.
MenuBar {
    id: control

    property color menuBarColor: "#171e2a"
    property color menuBarBorderColor: "#40516a"

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
